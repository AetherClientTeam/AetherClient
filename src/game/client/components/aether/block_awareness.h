#ifndef GAME_CLIENT_COMPONENTS_AETHER_BLOCK_AWARENESS_H
#define GAME_CLIENT_COMPONENTS_AETHER_BLOCK_AWARENESS_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

class CTeeRenderInfo;

class CAetherBlockAwareness : public CComponent
{
public:
	enum class EGroup
	{
		NONE,
		ENEMY,
		HELPER,
		ALLY,
		UNKNOWN,
	};

private:
	enum class EPopupKind
	{
		NONE,
		SAVE,
		BLOCK,
	};

	struct SPopup
	{
		bool m_Active = false;
		EPopupKind m_Kind = EPopupKind::NONE;
		int m_ClientId = -1;
		int m_DetectedTick = 0;
		char m_aName[MAX_NAME_LENGTH] = "";
		CUIRect m_HelperButton;
		CUIRect m_AllyButton;
		CUIRect m_IgnoreButton;
	};

	SPopup m_Popup;
	bool m_LocalWasFrozen = false;
	bool m_LocalWasDead = false;
	int m_LastProcessedTick = -1;
	int m_aLastSavePopupTick[MAX_CLIENTS] = {};
	int m_aLastBlockPopupTick[MAX_CLIENTS] = {};
	int m_aLastFreezeEnd[MAX_CLIENTS] = {};
	int m_aAllyFreezeAlertTick[MAX_CLIENTS] = {};

	bool IsLocalId(int ClientId) const;
	int FreezeEndTick(int ClientId) const;
	bool IsAlive(int ClientId) const;
	int FindSaveCandidate(int LocalId) const;
	int FindBlockCandidate(int LocalId) const;
	void OpenSavePopup(int ClientId);
	void OpenBlockPopup(int ClientId);
	void AddPopupClientToWarlist(const char *pType);
	void IgnoreSavePopup();
	void DrawPopupButton(const CUIRect &Rect, const char *pText, ColorRGBA Color);
	void RenderFreezeBars();
	void RenderLocalFreezeOverlay();
	void RenderAllyFreezeAlerts();
	void RenderEnemyCountAndDanger();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnNewSnapshot() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;

	bool IsEnabledForClient(int ClientId) const;
	bool IsFrozen(int ClientId) const;
	vec2 CharacterPos(int ClientId) const;
	EGroup GroupForClient(int ClientId) const;
	ColorRGBA ColorForGroup(EGroup Group, float Alpha = 1.0f) const;
	bool ShouldColorPlayer(int ClientId) const;
	bool ShouldUseDefaultColorSkin(int ClientId) const;
	bool ShouldColorName(int ClientId) const;
	bool ShouldScaleEnemy(int ClientId) const;
	float EnemyScale(int ClientId) const;
	float PlayerScale(int ClientId) const;
	float NameOpacity(int ClientId) const;
	ColorRGBA NameColorForClient(int ClientId, float BaseAlpha = 1.0f) const;
	void ApplyRenderInfo(int ClientId, CTeeRenderInfo &Info) const;
	bool DummyOwnerLabel(int ClientId, char *pBuf, int BufSize) const;
};

#endif
