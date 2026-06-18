#include "translate.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/protocol.h>

#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/localization.h>

#include <algorithm>
#include <memory>
#include <string>

static void UrlEncode(const char *pText, char *pOut, size_t Length)
{
	if(Length == 0)
		return;
	size_t OutPos = 0;
	for(const char *p = pText; *p && OutPos < Length - 1; ++p)
	{
		unsigned char c = *(const unsigned char *)p;
		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			if(OutPos >= Length - 1)
				break;
			pOut[OutPos++] = c;
		}
		else
		{
			if(OutPos + 3 >= Length)
				break;
			snprintf(pOut + OutPos, 4, "%%%02X", c);
			OutPos += 3;
		}
	}
	pOut[OutPos] = '\0';
}

static void NormalizeLibreTranslateEndpoint(char *pOut, int OutSize)
{
	pOut[0] = '\0';
	if(g_Config.m_TcTranslateEndpoint[0] == '\0')
		return;

	char aEndpoint[256];
	str_copy(aEndpoint, g_Config.m_TcTranslateEndpoint);
	while(aEndpoint[0] != '\0' && aEndpoint[str_length(aEndpoint) - 1] == '/')
		aEndpoint[str_length(aEndpoint) - 1] = '\0';

	if(str_find(aEndpoint, "://") == nullptr)
		str_format(pOut, OutSize, "https://%s", aEndpoint);
	else
		str_copy(pOut, aEndpoint, OutSize);

	if(str_find(pOut, "/translate") == nullptr)
		str_append(pOut, "/translate", OutSize);
}

static const char *EncodeGoogleSource(const char *pSource)
{
	if(!pSource || pSource[0] == '\0')
		return "auto";
	return pSource;
}

static bool IsAutoLanguage(const char *pLanguage)
{
	return pLanguage == nullptr || pLanguage[0] == '\0' || str_comp_nocase(pLanguage, "auto") == 0;
}

const char *ITranslateBackend::EncodeTarget(const char *pTarget) const
{
	if(!pTarget || pTarget[0] == '\0')
		return DefaultConfig::TcTranslateTarget;
	return pTarget;
}

bool ITranslateBackend::CompareTargets(const char *pA, const char *pB) const
{
	if(pA == pB) // if(!pA && !pB)
		return true;
	if(!pA || !pB)
		return false;
	if(str_comp_nocase(EncodeTarget(pA), EncodeTarget(pB)) == 0)
		return true;
	return false;
}

class ITranslateBackendHttp : public ITranslateBackend
{
protected:
	std::shared_ptr<CHttpRequest> m_pHttpRequest = nullptr;
	virtual bool ParseResponse(CTranslateResponse &Out) = 0;
	virtual bool ParseHttpError() const { return false; }

	void CreateHttpRequest(IHttp &Http, const char *pUrl)
	{
		auto pGet = std::make_shared<CHttpRequest>(pUrl);
		pGet->LogProgress(HTTPLOG::FAILURE);
		pGet->FailOnErrorStatus(false);
		pGet->Timeout(CTimeout{10000, 0, 500, 10});

		m_pHttpRequest = pGet;
		Http.Run(pGet);
	}

public:
	std::optional<bool> Update(CTranslateResponse &Out) override
	{
		dbg_assert(m_pHttpRequest != nullptr, "m_pHttpRequest is nullptr");
		if(m_pHttpRequest->State() == EHttpState::RUNNING || m_pHttpRequest->State() == EHttpState::QUEUED)
			return std::nullopt;
		if(m_pHttpRequest->State() == EHttpState::ABORTED)
		{
			str_copy(Out.m_Text, "Aborted");
			return false;
		}
		if(m_pHttpRequest->State() != EHttpState::DONE)
		{
			str_copy(Out.m_Text, "Curl error, see console");
			return false;
		}
		if(m_pHttpRequest->StatusCode() != 200 && !ParseHttpError())
		{
			str_format(Out.m_Text, sizeof(Out.m_Text), "Got http code %d", m_pHttpRequest->StatusCode());
			return false;
		}
		return ParseResponse(Out);
	}
	~ITranslateBackendHttp() override
	{
		if(m_pHttpRequest)
			m_pHttpRequest->Abort();
	}
};

class CTranslateBackendLibretranslate : public ITranslateBackendHttp
{
private:
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		if(pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pError = json_object_get(pObj, "error");
		if(pError != &json_value_none)
		{
			if(pError->type != json_string)
				str_copy(Out.m_Text, "Error is not string");
			else
				str_copy(Out.m_Text, pError->u.string.ptr);
			return false;
		}

		const json_value *pTranslatedText = json_object_get(pObj, "translatedText");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(Out.m_Text, "No translatedText");
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(Out.m_Text, "translatedText is not string");
			return false;
		}

		str_copy(Out.m_Text, pTranslatedText->u.string.ptr);
		const json_value *pDetectedLanguage = json_object_get(pObj, "detectedLanguage");
		if(pDetectedLanguage != &json_value_none && pDetectedLanguage->type == json_object)
		{
			const json_value *pLanguage = json_object_get(pDetectedLanguage, "language");
			if(pLanguage != &json_value_none && pLanguage->type == json_string)
				str_copy(Out.m_Language, pLanguage->u.string.ptr);
		}

		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Res;
	}
	bool ParseHttpError() const override { return true; }

public:
	const char *Name() const override
	{
		return "LibreTranslate";
	}
	CTranslateBackendLibretranslate(IHttp &Http, const char *pText)
	{
		CJsonStringWriter Json = CJsonStringWriter();
		Json.BeginObject();
		Json.WriteAttribute("q");
		Json.WriteStrValue(pText);
		Json.WriteAttribute("source");
		Json.WriteStrValue("auto");
		Json.WriteAttribute("target");
		Json.WriteStrValue(EncodeTarget(g_Config.m_TcTranslateTarget));
		Json.WriteAttribute("format");
		Json.WriteStrValue("text");
		if(g_Config.m_TcTranslateKey[0] != '\0')
		{
			Json.WriteAttribute("api_key");
			Json.WriteStrValue(g_Config.m_TcTranslateKey);
		}
		Json.EndObject();
		char aEndpoint[256];
		NormalizeLibreTranslateEndpoint(aEndpoint, sizeof(aEndpoint));
		CreateHttpRequest(Http, aEndpoint);
		const char *pJson = Json.GetOutputString().c_str();
		m_pHttpRequest->PostJson(pJson);
	}
};

class CTranslateBackendFtapi : public ITranslateBackendHttp
{
private:
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		if(pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pTranslatedText = json_object_get(pObj, "destination-text");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(Out.m_Text, "No destination-text");
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(Out.m_Text, "destination-text is not string");
			return false;
		}

		const json_value *pDetectedLanguage = json_object_get(pObj, "source-language");
		if(pDetectedLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No source-language");
			return false;
		}
		if(pDetectedLanguage->type != json_string)
		{
			str_copy(Out.m_Text, "source-language is not string");
			return false;
		}

		str_copy(Out.m_Text, pTranslatedText->u.string.ptr);
		str_copy(Out.m_Language, pDetectedLanguage->u.string.ptr);

		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Res;
	}

public:
	const char *EncodeTarget(const char *pTarget) const override
	{
		if(!pTarget || pTarget[0] == '\0')
			return DefaultConfig::TcTranslateTarget;
		if(str_comp_nocase(pTarget, "zh") == 0)
			return "zh-cn";
		return pTarget;
	}
	const char *Name() const override
	{
		return "FreeTranslateAPI";
	}
	CTranslateBackendFtapi(IHttp &Http, const char *pText)
	{
		char aBuf[4096];
		str_format(aBuf, sizeof(aBuf), "%s/translate?dl=%s&text=",
			g_Config.m_TcTranslateEndpoint[0] != '\0' ? g_Config.m_TcTranslateEndpoint : "https://ftapi.pythonanywhere.com",
			EncodeTarget(g_Config.m_TcTranslateTarget));

		UrlEncode(pText, aBuf + strlen(aBuf), sizeof(aBuf) - strlen(aBuf));

		CreateHttpRequest(Http, aBuf);
	}
};

class CTranslateBackendGoogle : public ITranslateBackendHttp
{
protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pRoot = m_pHttpRequest->ResultJson();
		if(!pRoot)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		bool Success = false;
		if(pRoot->type != json_array)
		{
			str_copy(Out.m_Text, "Response is not array");
		}
		else
		{
			const json_value *pSentences = json_array_get(pRoot, 0);
			if(!pSentences || pSentences->type != json_array)
			{
				str_copy(Out.m_Text, "Missing translation entries");
			}
			else
			{
				std::string Result;
				for(int i = 0; i < json_array_length(pSentences); ++i)
				{
					const json_value *pSentence = json_array_get(pSentences, i);
					if(!pSentence || pSentence->type != json_array)
						continue;
					const json_value *pTranslated = json_array_get(pSentence, 0);
					if(pTranslated && pTranslated->type == json_string)
						Result += pTranslated->u.string.ptr;
				}

				if(Result.empty())
				{
					str_copy(Out.m_Text, "Translation is empty");
				}
				else
				{
					str_copy(Out.m_Text, Result.c_str(), sizeof(Out.m_Text));
					const json_value *pDetectedLanguage = json_array_get(pRoot, 2);
					if(pDetectedLanguage && pDetectedLanguage->type == json_string)
						str_copy(Out.m_Language, pDetectedLanguage->u.string.ptr, sizeof(Out.m_Language));
					Success = true;
				}
			}
		}

		json_value_free(pRoot);
		return Success;
	}

public:
	const char *EncodeTarget(const char *pTarget) const override
	{
		if(!pTarget || pTarget[0] == '\0')
			return DefaultConfig::TcTranslateTarget;
		if(str_comp_nocase(pTarget, "zh") == 0)
			return "zh-cn";
		return pTarget;
	}
	const char *Name() const override
	{
		return "Google Translate";
	}
	CTranslateBackendGoogle(IHttp &Http, const char *pText)
	{
		char aBuf[4096];
		str_format(aBuf, sizeof(aBuf), "https://translate.googleapis.com/translate_a/single?client=gtx&sl=%s&tl=%s&dt=t&q=",
			EncodeGoogleSource("auto"), EncodeTarget(g_Config.m_TcTranslateTarget));
		UrlEncode(pText, aBuf + strlen(aBuf), sizeof(aBuf) - strlen(aBuf));
		CreateHttpRequest(Http, aBuf);
	}
};

static std::unique_ptr<ITranslateBackend> CreateEmbeddedTranslateBackend(IHttp &Http, const char *pText)
{
	return std::make_unique<CTranslateBackendGoogle>(Http, pText);
}

void CTranslate::ConTranslate(IConsole::IResult *pResult, void *pUserData)
{
	const char *pName;
	if(pResult->NumArguments() == 0)
		pName = nullptr;
	else
		pName = pResult->GetString(0);

	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pName);
}

void CTranslate::ConTranslateId(IConsole::IResult *pResult, void *pUserData)
{
	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pResult->GetInteger(0));
}

void CTranslate::OnConsoleInit()
{
	// Stop legacy auto-translate from forcing every incoming line after the new explicit toggle exists.
	if(g_Config.m_TcTranslateAuto)
		g_Config.m_TcTranslateAuto = 0;
	if(IsAutoLanguage(g_Config.m_TcTranslateTarget))
		str_copy(g_Config.m_TcTranslateTarget, DefaultConfig::TcTranslateTarget, sizeof(g_Config.m_TcTranslateTarget));
	str_copy(g_Config.m_TcTranslateBackend, "google", sizeof(g_Config.m_TcTranslateBackend));
	Console()->Register("translate", "?r[name]", CFGFLAG_CLIENT, ConTranslate, this, "Translate last message (of a given name)");
	Console()->Register("translate_id", "v[id]", CFGFLAG_CLIENT, ConTranslateId, this, "Translate last message of the person with this id");
}

void CTranslate::Translate(int Id, bool ShowProgress)
{
	if(Id < 0 || Id > (int)std::size(GameClient()->m_aClients))
	{
		GameClient()->m_Chat.Echo("Not a valid ID");
		return;
	}
	const auto &Player = GameClient()->m_aClients[Id];
	if(!Player.m_Active)
	{
		GameClient()->m_Chat.Echo("ID not connected");
		return;
	}
	Translate(Player.m_aName, ShowProgress);
}

void CTranslate::Translate(const char *pName, bool ShowProgress)
{
	CChat::CLine *pLineBest = nullptr;
	if(GameClient()->m_Chat.m_CurrentLine > 0)
	{
		int ScoreBest = -1;
		for(int i = 0; i < CChat::MAX_LINES; i++)
		{
			CChat::CLine *pLine = &GameClient()->m_Chat.m_aLines[((GameClient()->m_Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES];
			if(pLine->m_pTranslateResponse != nullptr)
				continue;
			if(pLine->m_ClientId == CChat::CLIENT_MSG)
				continue;
			for(int Id : GameClient()->m_aLocalIds)
				if(pLine->m_ClientId == Id)
					continue;
			int Score = 0;
			if(pName)
			{
				if(pLine->m_ClientId == CChat::SERVER_MSG)
					continue;
				if(str_comp(pLine->m_aName, pName) == 0)
					Score = 2;
				else if(str_comp_nocase(pLine->m_aName, pName) == 0)
					Score = 1;
				else
					continue;
			}
			if(Score > ScoreBest)
			{
				ScoreBest = Score;
				pLineBest = pLine;
			}
		}
	}
	if(!pLineBest || pLineBest->m_aText[0] == '\0')
		return;

	Translate(*pLineBest, ShowProgress);
}

void CTranslate::Translate(CChat::CLine &Line, bool ShowProgress)
{
	if(m_vJobs.size() > 15)
	{
		return;
	}

	CTranslateJob Job;
	Job.m_pLine = &Line;
	Job.m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	Job.m_pLine->m_pTranslateResponse = Job.m_pTranslateResponse;
	Job.m_pBackend = CreateEmbeddedTranslateBackend(*Http(), Job.m_pLine->m_aText);

	if(ShowProgress)
	{
		str_format(Job.m_pTranslateResponse->m_Text, sizeof(Job.m_pTranslateResponse->m_Text), TCLocalize("%s translating to %s", "translate"), Job.m_pBackend->Name(), g_Config.m_TcTranslateTarget);
		Job.m_pLine->m_Time = time();
	}
	else
	{
		Job.m_pTranslateResponse->m_Text[0] = '\0';
	}

	m_vJobs.emplace_back(std::move(Job));

	if(ShowProgress)
		GameClient()->m_Chat.RebuildChat();
}

void CTranslate::OnRender()
{
	const auto Time = time();
	auto ForEach = [&](CTranslateJob &Job) {
		if(Job.m_pLine->m_pTranslateResponse != Job.m_pTranslateResponse)
			return true; // Not the same line anymore
		const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
		if(!Done.has_value())
			return false; // Keep ongoing tasks
		if(*Done)
		{
			if(str_comp_nocase(Job.m_pLine->m_aText, Job.m_pTranslateResponse->m_Text) == 0) // Check for no translation difference
				Job.m_pTranslateResponse->m_Text[0] = '\0';
		}
		else
		{
			char aBuf[sizeof(Job.m_pTranslateResponse->m_Text)];
			str_format(aBuf, sizeof(aBuf), TCLocalize("%s to %s failed: %s", "translate"), Job.m_pBackend->Name(), g_Config.m_TcTranslateTarget, Job.m_pTranslateResponse->m_Text);
			Job.m_pTranslateResponse->m_Error = true;
			str_copy(Job.m_pTranslateResponse->m_Text, aBuf);
		}
		Job.m_pLine->m_Time = Time;
		GameClient()->m_Chat.RebuildChat();
		return true;
	};
	m_vJobs.erase(std::remove_if(m_vJobs.begin(), m_vJobs.end(), ForEach), m_vJobs.end());

	auto ForEachOutgoing = [&](COutgoingTranslateJob &Job) {
		CTranslateResponse Response;
		const std::optional<bool> Done = Job.m_pBackend->Update(Response);
		if(!Done.has_value())
			return false;
		if(*Done)
		{
			const char *pMessage = Response.m_Text[0] != '\0' ? Response.m_Text : Job.m_aOriginal;
			GameClient()->m_Chat.SendChat(Job.m_Team, pMessage);
		}
		else
		{
			char aBuf[sizeof(Response.m_Text) + 64];
			str_format(aBuf, sizeof(aBuf), "Outgoing translate to %s failed: %s", g_Config.m_TcTranslateTarget, Response.m_Text);
			GameClient()->m_Chat.Echo(aBuf);
			GameClient()->m_Chat.SendChat(Job.m_Team, Job.m_aOriginal);
		}
		return true;
	};
	m_vOutgoingJobs.erase(std::remove_if(m_vOutgoingJobs.begin(), m_vOutgoingJobs.end(), ForEachOutgoing), m_vOutgoingJobs.end());
}

void CTranslate::AutoTranslate(CChat::CLine &Line)
{
	if(!g_Config.m_TcTranslateAutoIncoming)
		return;
	if(IsAutoLanguage(g_Config.m_TcTranslateTarget))
		return;
	if(Line.m_ClientId == CChat::CLIENT_MSG)
		return;
	for(const int Id : GameClient()->m_aLocalIds)
	{
		if(Id >= 0 && Id == Line.m_ClientId)
			return;
	}
	Translate(Line, false);
}

bool CTranslate::TranslateOutgoing(int Team, const char *pLine)
{
	if(!g_Config.m_TcTranslateOutgoing)
		return false;
	if(IsAutoLanguage(g_Config.m_TcTranslateTarget))
		return false;
	if(!pLine || pLine[0] == '\0')
		return false;
	if(pLine[0] == '/')
		return false;
	if(m_vOutgoingJobs.size() >= 3)
	{
		GameClient()->m_Chat.Echo("Outgoing translate queue is full.");
		return true;
	}

	COutgoingTranslateJob Job;
	Job.m_Team = Team;
	str_copy(Job.m_aOriginal, pLine, sizeof(Job.m_aOriginal));
	Job.m_pBackend = CreateEmbeddedTranslateBackend(*Http(), pLine);
	m_vOutgoingJobs.emplace_back(std::move(Job));
	return true;
}
