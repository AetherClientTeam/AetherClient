#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
HWND g_Window = nullptr;
std::wstring g_Status = L"Preparing update...";
std::wstring g_Detail;
int g_Progress = 0;
bool g_Done = false;
bool g_Failed = false;
fs::path g_LogPath;

constexpr UINT WM_AETHER_PROGRESS = WM_APP + 1;

RECT CloseButtonRect(HWND Window)
{
	RECT Client{};
	GetClientRect(Window, &Client);
	return RECT{Client.right - 42, 12, Client.right - 14, 40};
}

fs::path CurrentExecutablePath()
{
	std::vector<wchar_t> Buffer(MAX_PATH);
	for(;;)
	{
		const DWORD Length = GetModuleFileNameW(nullptr, Buffer.data(), (DWORD)Buffer.size());
		if(Length == 0)
			return {};
		if(Length < Buffer.size() - 1)
			return fs::path(std::wstring(Buffer.data(), Length));
		Buffer.resize(Buffer.size() * 2);
	}
}

bool SamePath(const fs::path &Left, const fs::path &Right)
{
	std::error_code Ec;
	if(fs::equivalent(Left, Right, Ec))
		return true;
	std::wstring LeftPath = fs::absolute(Left, Ec).wstring();
	std::wstring RightPath = fs::absolute(Right, Ec).wstring();
	return _wcsicmp(LeftPath.c_str(), RightPath.c_str()) == 0;
}

bool PointInRect(const RECT &Rect, int X, int Y)
{
	return X >= Rect.left && X <= Rect.right && Y >= Rect.top && Y <= Rect.bottom;
}

std::wstring Quote(const fs::path &Path)
{
	return L"\"" + Path.wstring() + L"\"";
}

std::string Utf8FromWide(const std::wstring &Text)
{
	if(Text.empty())
		return {};
	const int Length = WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if(Length <= 0)
		return {};
	std::string Result(Length - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), -1, Result.data(), Length, nullptr, nullptr);
	return Result;
}

void Log(const std::wstring &Line)
{
	try
	{
		if(g_LogPath.empty())
			return;
		std::ofstream File(g_LogPath, std::ios::app | std::ios::binary);
		File << Utf8FromWide(Line) << "\n";
	}
	catch(...)
	{
	}
}

void SetProgress(int Progress, const std::wstring &Status, const std::wstring &Detail = L"")
{
	g_Progress = Progress;
	g_Status = Status;
	g_Detail = Detail;
	Log(Status + (Detail.empty() ? L"" : L" - " + Detail));
	if(g_Window)
		PostMessageW(g_Window, WM_AETHER_PROGRESS, 0, 0);
}

std::wstring WideFromMultiByte(const char *pText)
{
	if(!pText || pText[0] == '\0')
		return {};

	const int Utf8Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pText, -1, nullptr, 0);
	if(Utf8Length > 0)
	{
		std::wstring Result(Utf8Length - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pText, -1, Result.data(), Utf8Length);
		return Result;
	}

	const int AcpLength = MultiByteToWideChar(CP_ACP, 0, pText, -1, nullptr, 0);
	if(AcpLength <= 0)
		return L"Unknown error.";
	std::wstring Result(AcpLength - 1, L'\0');
	MultiByteToWideChar(CP_ACP, 0, pText, -1, Result.data(), AcpLength);
	return Result;
}

std::wstring FriendlyFilesystemError(const fs::filesystem_error &Error)
{
	const int ErrorValue = Error.code().value();
	if(ErrorValue == ERROR_SHARING_VIOLATION || ErrorValue == ERROR_LOCK_VIOLATION)
		return L"Dosya \u015Fu anda ba\u015Fka bir i\u015Flem taraf\u0131ndan kullan\u0131l\u0131yor. Aether'i kapat\u0131p tekrar deneyin.";
	if(ErrorValue == ERROR_ACCESS_DENIED)
		return L"Dosyaya eri\u015Fim izni yok. Aether'i kapat\u0131p y\u00F6netici olarak tekrar deneyin.";

	std::wstring Message = WideFromMultiByte(Error.what());
	if(Message.empty())
		Message = L"Update failed while copying files.";
	if(Message.rfind(L"copy:", 0) == 0)
		Message = L"Kopyalama ba\u015Far\u0131s\u0131z:" + Message.substr(5);
	return Message;
}

std::wstring FriendlyException(const std::exception &Error)
{
	std::wstring Message = WideFromMultiByte(Error.what());
	if(Message.empty())
		return L"Unknown error.";
	if(Message.rfind(L"copy:", 0) == 0)
		Message = L"Kopyalama ba\u015Far\u0131s\u0131z:" + Message.substr(5);
	return Message;
}

bool RunHiddenAndWait(const std::wstring &Application, const std::wstring &Arguments, const fs::path &WorkingDir, DWORD *pExitCode)
{
	STARTUPINFOW StartupInfo{};
	PROCESS_INFORMATION ProcessInfo{};
	StartupInfo.cb = sizeof(StartupInfo);
	StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
	StartupInfo.wShowWindow = SW_HIDE;

	std::wstring CommandLine = Quote(Application) + L" " + Arguments;
	std::vector<wchar_t> MutableCommandLine(CommandLine.begin(), CommandLine.end());
	MutableCommandLine.push_back(L'\0');

	BOOL Result = CreateProcessW(
		nullptr,
		MutableCommandLine.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr,
		WorkingDir.empty() ? nullptr : WorkingDir.c_str(),
		&StartupInfo,
		&ProcessInfo);
	if(!Result)
		return false;

	WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
	DWORD ExitCode = 1;
	GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
	CloseHandle(ProcessInfo.hThread);
	CloseHandle(ProcessInfo.hProcess);
	if(pExitCode)
		*pExitCode = ExitCode;
	return ExitCode == 0;
}

bool ExtractArchive(const fs::path &ArchivePath, const fs::path &ExtractDir)
{
	DWORD ExitCode = 1;
	const std::wstring TarArgs = L"-xf " + Quote(ArchivePath) + L" -C " + Quote(ExtractDir);
	if(RunHiddenAndWait(L"tar.exe", TarArgs, ExtractDir, &ExitCode))
		return true;

	Log(L"tar.exe extraction failed, trying hidden PowerShell fallback.");
	const std::wstring PsArgs =
		L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
		L"\"Expand-Archive -LiteralPath " +
		Quote(ArchivePath) +
		L" -DestinationPath " +
		Quote(ExtractDir) +
		L" -Force\"";
	return RunHiddenAndWait(L"powershell.exe", PsArgs, ExtractDir, &ExitCode);
}

fs::path DetectCopyRoot(const fs::path &ExtractDir)
{
	std::vector<fs::directory_entry> Entries;
	for(const auto &Entry : fs::directory_iterator(ExtractDir))
		Entries.push_back(Entry);
	if(Entries.size() == 1 && Entries[0].is_directory())
		return Entries[0].path();
	return ExtractDir;
}

void CopyTree(const fs::path &Source, const fs::path &Destination)
{
	const fs::path UpdaterPath = CurrentExecutablePath();
	for(const auto &Entry : fs::directory_iterator(Source))
	{
		const fs::path Target = Destination / Entry.path().filename();
		if(Entry.is_directory())
		{
			fs::create_directories(Target);
			CopyTree(Entry.path(), Target);
		}
		else
		{
			if(!UpdaterPath.empty() && SamePath(Target, UpdaterPath))
			{
				fs::copy_file(Entry.path(), Target.wstring() + L".new", fs::copy_options::overwrite_existing);
				continue;
			}
			fs::copy_file(Entry.path(), Target, fs::copy_options::overwrite_existing);
		}
	}
}

bool WaitForProcessExit(DWORD Pid)
{
	if(Pid == 0)
		return true;
	HANDLE Process = OpenProcess(SYNCHRONIZE, FALSE, Pid);
	if(!Process)
		return true;
	const DWORD Result = WaitForSingleObject(Process, 30000);
	CloseHandle(Process);
	return Result == WAIT_OBJECT_0;
}

std::wstring ArgValue(int Argc, wchar_t **Argv, const wchar_t *pName)
{
	for(int i = 1; i + 1 < Argc; ++i)
	{
		if(lstrcmpiW(Argv[i], pName) == 0)
			return Argv[i + 1];
	}
	return L"";
}

DWORD ArgDword(int Argc, wchar_t **Argv, const wchar_t *pName)
{
	const std::wstring Value = ArgValue(Argc, Argv, pName);
	if(Value.empty())
		return 0;
	return wcstoul(Value.c_str(), nullptr, 10);
}

void Fail(const std::wstring &Message)
{
	g_Failed = true;
	g_Done = true;
	SetProgress(g_Progress, L"Update failed", Message);
}

void Worker(int Argc, wchar_t **Argv)
{
	const fs::path InstallDir = ArgValue(Argc, Argv, L"--install-dir");
	const fs::path ArchivePath = ArgValue(Argc, Argv, L"--archive");
	const fs::path ExePath = ArgValue(Argc, Argv, L"--exe");
	const DWORD PidToWait = ArgDword(Argc, Argv, L"--pid");

	if(InstallDir.empty() || ArchivePath.empty() || ExePath.empty())
	{
		Fail(L"Updater was launched without required paths.");
		return;
	}

	g_LogPath = InstallDir / L"update" / L"update-log.txt";
	try
	{
		fs::create_directories(g_LogPath.parent_path());
		Log(L"=== Aether updater started ===");
		Log(L"InstallDir: " + InstallDir.wstring());
		Log(L"Archive: " + ArchivePath.wstring());

		SetProgress(5, L"Waiting for Aether to close...");
		if(!WaitForProcessExit(PidToWait))
		{
			Fail(L"Aether did not close in time. Please close it manually and try again.");
			return;
		}

		if(!fs::exists(ArchivePath))
		{
			Fail(L"Downloaded update archive was not found.");
			return;
		}

		const fs::path UpdateDir = InstallDir / L"update";
		const fs::path ExtractDir = UpdateDir / L"extract";
		SetProgress(18, L"Preparing files...");
		fs::remove_all(ExtractDir);
		fs::create_directories(ExtractDir);

		SetProgress(35, L"Extracting update...");
		if(!ExtractArchive(ArchivePath, ExtractDir))
		{
			Fail(L"Could not extract the update archive. See update-log.txt.");
			return;
		}

		const fs::path CopyRoot = DetectCopyRoot(ExtractDir);
		const fs::path RequiredClient = CopyRoot / L"Aether.exe";
		const fs::path RequiredServer = CopyRoot / L"Aether-Server.exe";
		const fs::path RequiredData = CopyRoot / L"data" / L"core";
		if(!fs::exists(RequiredClient) || !fs::exists(RequiredServer) || !fs::exists(RequiredData))
		{
			Fail(L"Update archive is missing Aether.exe, Aether-Server.exe or data/core.");
			return;
		}

		SetProgress(62, L"Installing files...");
		CopyTree(CopyRoot, InstallDir);

		SetProgress(78, L"Cleaning old files...");
		for(const wchar_t *pLegacyExe : {L"Vera.exe", L"Via.exe", L"Vex.exe"})
		{
			std::error_code Ec;
			fs::remove(InstallDir / pLegacyExe, Ec);
		}
		std::error_code Ec;
		if(fs::exists(InstallDir / L"data" / L"core"))
			fs::remove_all(InstallDir / L"data" / L"aether", Ec);
		fs::remove(ArchivePath, Ec);
		fs::remove_all(ExtractDir, Ec);

		SetProgress(92, L"Starting Aether...");
		const fs::path Relaunch = InstallDir / L"Aether.exe";
		ShellExecuteW(nullptr, L"open", Relaunch.c_str(), nullptr, InstallDir.c_str(), SW_SHOWNORMAL);

		SetProgress(100, L"Update complete.");
		g_Done = true;
		PostMessageW(g_Window, WM_AETHER_PROGRESS, 0, 0);
		std::this_thread::sleep_for(std::chrono::milliseconds(900));
		PostMessageW(g_Window, WM_CLOSE, 0, 0);
	}
	catch(const fs::filesystem_error &e)
	{
		Fail(FriendlyFilesystemError(e));
	}
	catch(const std::exception &e)
	{
		Fail(FriendlyException(e));
	}
	catch(...)
	{
		Fail(L"Unknown updater error.");
	}
}

void Paint(HWND Window)
{
	PAINTSTRUCT Ps;
	HDC Dc = BeginPaint(Window, &Ps);
	RECT Client{};
	GetClientRect(Window, &Client);

	HBRUSH Bg = CreateSolidBrush(RGB(15, 15, 19));
	FillRect(Dc, &Client, Bg);
	DeleteObject(Bg);

	SetBkMode(Dc, TRANSPARENT);
	SetTextColor(Dc, RGB(242, 235, 250));

	HFONT TitleFont = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	HFONT TextFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

	HFONT OldFont = (HFONT)SelectObject(Dc, TitleFont);
	RECT Title{0, 22, Client.right, 90};
	DrawTextW(Dc, L"Aether Updater", -1, &Title, DT_CENTER | DT_SINGLELINE);

	SelectObject(Dc, TextFont);
	SetTextColor(Dc, (g_Done || g_Failed) ? RGB(206, 194, 218) : RGB(82, 78, 90));
	RECT CloseRect = CloseButtonRect(Window);
	DrawTextW(Dc, L"X", -1, &CloseRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	SetTextColor(Dc, RGB(242, 235, 250));
	RECT Bar{58, 122, Client.right - 94, 154};
	HBRUSH BarBg = CreateSolidBrush(RGB(39, 39, 44));
	FillRect(Dc, &Bar, BarBg);
	DeleteObject(BarBg);

	RECT Fill = Bar;
	Fill.right = Fill.left + (Bar.right - Bar.left) * g_Progress / 100;
	HBRUSH FillBrush = CreateSolidBrush(RGB(218, 162, 223));
	FillRect(Dc, &Fill, FillBrush);
	DeleteObject(FillBrush);

	std::wstring Percent = std::to_wstring(g_Progress) + L"%";
	RECT PercentRect{Bar.right + 12, Bar.top + 4, Client.right - 30, Bar.bottom};
	DrawTextW(Dc, Percent.c_str(), -1, &PercentRect, DT_LEFT | DT_SINGLELINE);

	SetTextColor(Dc, g_Failed ? RGB(255, 135, 150) : RGB(202, 193, 212));
	RECT Status{58, 172, Client.right - 58, 198};
	DrawTextW(Dc, g_Status.c_str(), -1, &Status, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

	if(!g_Detail.empty())
	{
		SetTextColor(Dc, RGB(150, 145, 160));
		RECT Detail{58, 200, Client.right - 58, Client.bottom - 24};
		DrawTextW(Dc, g_Detail.c_str(), -1, &Detail, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
	}

	SelectObject(Dc, OldFont);
	DeleteObject(TitleFont);
	DeleteObject(TextFont);
	EndPaint(Window, &Ps);
}

LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	switch(Message)
	{
	case WM_AETHER_PROGRESS:
		InvalidateRect(Window, nullptr, FALSE);
		return 0;
	case WM_LBUTTONDOWN:
	{
		const int X = GET_X_LPARAM(LParam);
		const int Y = GET_Y_LPARAM(LParam);
		if(!PointInRect(CloseButtonRect(Window), X, Y) && Y < 58)
		{
			ReleaseCapture();
			SendMessageW(Window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
			return 0;
		}
		break;
	}
	case WM_LBUTTONUP:
	{
		const int X = GET_X_LPARAM(LParam);
		const int Y = GET_Y_LPARAM(LParam);
		if((g_Done || g_Failed) && PointInRect(CloseButtonRect(Window), X, Y))
		{
			DestroyWindow(Window);
			return 0;
		}
		break;
	}
	case WM_KEYDOWN:
		if((g_Done || g_Failed) && WParam == VK_ESCAPE)
		{
			DestroyWindow(Window);
			return 0;
		}
		break;
	case WM_PAINT:
		Paint(Window);
		return 0;
	case WM_CLOSE:
		if(!g_Done || g_Failed)
			DestroyWindow(Window);
		else
			DestroyWindow(Window);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(Window, Message, WParam, LParam);
	}
	return DefWindowProcW(Window, Message, WParam, LParam);
}
}

int WINAPI wWinMain(HINSTANCE Instance, HINSTANCE, PWSTR, int)
{
	int Argc = 0;
	wchar_t **Argv = CommandLineToArgvW(GetCommandLineW(), &Argc);

	WNDCLASSW WindowClass{};
	WindowClass.lpfnWndProc = WindowProc;
	WindowClass.hInstance = Instance;
	WindowClass.lpszClassName = L"AetherUpdaterWindow";
	WindowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClassW(&WindowClass);

	const int Width = 560;
	const int Height = 280;
	const int X = (GetSystemMetrics(SM_CXSCREEN) - Width) / 2;
	const int Y = (GetSystemMetrics(SM_CYSCREEN) - Height) / 2;
	g_Window = CreateWindowExW(WS_EX_APPWINDOW, WindowClass.lpszClassName, L"Aether Updater", WS_POPUP, X, Y, Width, Height, nullptr, nullptr, Instance, nullptr);
	ShowWindow(g_Window, SW_SHOW);
	UpdateWindow(g_Window);

	std::thread Thread([Argc, Argv]() {
		Worker(Argc, Argv);
		if(Argv)
			LocalFree(Argv);
	});
	Thread.detach();

	MSG Message;
	while(GetMessageW(&Message, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&Message);
		DispatchMessageW(&Message);
	}
	return g_Failed ? 1 : 0;
}
