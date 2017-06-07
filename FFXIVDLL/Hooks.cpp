#pragma comment(lib, "ws2_32.lib")
#include <Winsock2.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include <Psapi.h>
#include <windowsx.h>
#include "FFXIVDLL.h"
#include "../TsudaKageyu-minhook/include/MinHook.h"

bool Hooks::mHookStarted = false;
std::atomic_int Hooks::mHookedFunctionDepth;
HMODULE Hooks::hGame;
OverlayRenderer *Hooks::pOverlayRenderer;
FFXIVDLL *Hooks::dll;
WNDPROC Hooks::ffxivWndProc;
int Hooks::ffxivWndPressed = 0;
WindowControllerBase *Hooks::ffxivHookCaptureControl = 0;
WindowControllerBase *Hooks::lastHover = 0;
bool Hooks::mLockCursor = false;
void *Hooks::chatObject = 0;
char *Hooks::chatPtrs[4] = { (char*) 0x06, 0, (char*) 0x06, 0 };
Hooks::HOOKS_ORIG_FN_SET Hooks::pfnOrig = { 0 };
Hooks::HOOKS_BRIDGE_FN_SET Hooks::pfnBridge = { 0 };

#ifdef _WIN64
PVOID *Hooks::pDX11SwapChainTable;
extern LRESULT ImGui_ImplDX11_WndProcHandler(HWND, UINT msg, WPARAM wParam, LPARAM lParam);
#else
PVOID *Hooks::pDX9Table;
extern LRESULT ImGui_ImplDX9_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

DWORD_PTR testRead(DWORD_PTR *p) {
	__try {
		return *p;
	} __except (1) {
		return 0;
	}
}


Hooks::Hooks(FFXIVDLL *dll) {
	if (MH_OK != MH_Initialize())
		return;

	Hooks::dll = dll;

	DWORD cbNeeded;
	EnumProcessModulesEx(GetCurrentProcess(), &hGame, sizeof(hGame), &cbNeeded, LIST_MODULES_32BIT | LIST_MODULES_64BIT);

	IMAGE_NT_HEADERS *peHeader = (IMAGE_NT_HEADERS*) ((DWORD_PTR) hGame + (DWORD_PTR) ((PIMAGE_DOS_HEADER) hGame)->e_lfanew);
	IMAGE_SECTION_HEADER *sectionHeaders = (IMAGE_SECTION_HEADER*) &(peHeader[1]);
	for (int i = 0; i < peHeader->FileHeader.NumberOfSections; i++, sectionHeaders++)
		if (0 == strncmp((char*) sectionHeaders->Name, ".text", 5))
			goto textSectionFound;
	return;
textSectionFound:

#ifdef _WIN64
#else
	pfnOrig.ProcessWindowMessage = (ProcessWindowMessage) Tools::FindPattern((DWORD) hGame + sectionHeaders->VirtualAddress, sectionHeaders->Misc.VirtualSize, (PBYTE) "\x55\x8B\xEC\x83\xEC\x1C\x53\x8B\x1D\x00\x00\x00\x00\x57\x6A\x00", "xxxxxxxxx????xxx");
	pfnOrig.ProcessNewLine = (ProcessNewLine) (hGame + sectionHeaders->VirtualAddress - 1);
	do {
		pfnOrig.ProcessNewLine = (ProcessNewLine) Tools::FindPattern((DWORD) pfnOrig.ProcessNewLine + 1,
			sectionHeaders->Misc.VirtualSize - (DWORD) pfnOrig.ProcessNewLine + (DWORD) (hGame + sectionHeaders->VirtualAddress),
			(PBYTE) "\x55\x8B\xEC\x81\xEC\xAC\x00\x00\x00\xA1\x00\x00\x00\x00\x33\xC5\x89\x45\xFC\x53\x8B\x5D", "xxxxxxxxxx????xxxxxxxx");
		if (pfnOrig.ProcessNewLine && 0 != Tools::FindPattern((DWORD) pfnOrig.ProcessNewLine, 0x300,
			(PBYTE) "\xFF\x83\x7D\x10\x02\x7F\x16\x8D\x85\x54\xFF\xFF\xFF\x50\x8D\x4D\xA8\x51\x8D",
			"xxxxxxxxxxxxxxxxxxx"))
			break;
	} while (pfnOrig.ProcessNewLine && (int) (sectionHeaders->Misc.VirtualSize - (DWORD) pfnOrig.ProcessNewLine + (DWORD) (hGame + sectionHeaders->VirtualAddress)) > 0);
	pfnOrig.OnNewChatItem = (OnNewChatItem) Tools::FindPattern((DWORD) hGame + sectionHeaders->VirtualAddress, sectionHeaders->Misc.VirtualSize, (PBYTE) "\x55\x8B\xEC\x51\x8B\x45\x0C\x53\x56\x8B\xD9\x57\x8B\x7B\x4C\x2B\x7B\x48\x8D\x4B\x48\x03\xC7\x50\x89\x45\xFC\xE8\x60\x1F\x00\x00\x8B\x4D\x0C\x8B\x55\x08\x8B\x43\x48\x51\x52\x03\xC7\x50\xE8", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
#endif
	MH_CreateHook(pfnOrig.ProcessWindowMessage, hook_ProcessWindowMessage, (PVOID*) &pfnBridge.ProcessWindowMessage);
	MH_CreateHook(pfnOrig.ProcessNewLine, hook_ProcessNewLine, (PVOID*) &pfnBridge.ProcessNewLine);
	MH_CreateHook(pfnOrig.OnNewChatItem, hook_OnNewChatItem, (PVOID*) &pfnBridge.OnNewChatItem);

	ffxivWndProc = (WNDPROC) SetWindowLongPtr(dll->ffxiv(), GWLP_WNDPROC, (LONG_PTR) hook_ffxivWndProc);

	MH_CreateHook(recv, hook_socket_recv, NULL);
	MH_CreateHook(send, hook_socket_send, NULL);
	MH_CreateHook(SetCursor, hook_WinApiSetCursor, (LPVOID*) &pfnBridge.WinApiSetCursor);

#ifdef _WIN64

	HWND tempHwnd;
	WNDCLASS wc = { 0 };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hGame;
	wc.hbrBackground = (HBRUSH) (COLOR_BACKGROUND);
	wc.lpszClassName = L"TESTDX11DEV";

	RegisterClass(&wc);
	tempHwnd = CreateWindowEx(
		WS_EX_CLIENTEDGE,
		L"TESTDX11DEV",
		L"The title of my window",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
		NULL, NULL, wc.hInstance, NULL);

	ID3D11Device *pDevice;
	ID3D11DeviceContext *context = nullptr;
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 1;
	sd.BufferDesc.Width = 640;
	sd.BufferDesc.Height = 480;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = tempHwnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;

	IDXGISwapChain* m_pTempSwapChain = nullptr;
	ID3D11Device* m_pTempDevice = nullptr;
	ID3D11DeviceContext* m_pTempContext = nullptr;
	D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_DEBUG, &featureLevel, 1, D3D11_SDK_VERSION, &m_pTempDevice, NULL, &m_pTempContext);

	IDXGIDevice* dxgiDevice = nullptr;
	IDXGIFactory1* dxgiFactory = nullptr;
	IDXGIAdapter* adapter = nullptr;
	if (m_pTempDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) goto clear_dx_alloc;
	if (dxgiDevice->GetAdapter(&adapter)) goto clear_dx_alloc;
	if (adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory))) goto clear_dx_alloc;
	if (dxgiFactory->CreateSwapChain(m_pTempDevice, &sd, &m_pTempSwapChain)) goto clear_dx_alloc;
	if (m_pTempDevice != nullptr) {

		pDX11SwapChainTable = (PVOID*)*(PVOID*) m_pTempSwapChain;

		MH_CreateHook(pDX11SwapChainTable[8],
			hook_Dx11SwapChain_Present,
			(LPVOID*) &pfnBridge.Dx11SwapChainPresent);
	}

clear_dx_alloc:
	SAFE_RELEASE(m_pTempSwapChain);
	SAFE_RELEASE(dxgiFactory);
	SAFE_RELEASE(adapter);
	SAFE_RELEASE(dxgiDevice);
	SAFE_RELEASE(m_pTempContext);
	SAFE_RELEASE(m_pTempDevice);

	DestroyWindow(tempHwnd);
	UnregisterClass(L"TESTDX11DEV", hGame);

#else
	pDX9Table = (PVOID*)*((DWORD*) (2 + Tools::FindPattern((DWORD) GetModuleHandle(L"d3d9.dll"), 0x128000, (PBYTE)"\xC7\x06\x00\x00\x00\x00\x89\x86\x00\x00\x00\x00\x89\x86", "xx????xx????xx")));

	MH_CreateHook((LPVOID) pDX9Table[DX9_ENDSCENE], hook_Dx9EndScene, (LPVOID*) &pfnBridge.Dx9EndScene);
	MH_CreateHook((LPVOID) pDX9Table[DX9_RESET], hook_Dx9Reset, (LPVOID*) &pfnBridge.Dx9Reset);
	MH_CreateHook((LPVOID) pDX9Table[DX9_PRESENT], hook_Dx9Present, (LPVOID*) &pfnBridge.Dx9Present);
#endif
}

Hooks::~Hooks() {

	SetWindowLongPtr(dll->ffxiv(), GWLP_WNDPROC, (LONG_PTR) ffxivWndProc);

	int mainThread = Tools::GetMainThreadID(GetCurrentProcessId());
	HANDLE mainThreadHandle = OpenThread(THREAD_SUSPEND_RESUME, false, mainThread);
	SuspendThread(mainThreadHandle);
	MH_DisableHook(MH_ALL_HOOKS);
	ResumeThread(mainThreadHandle);
	CloseHandle(mainThreadHandle);

	while (mHookedFunctionDepth > 0) Sleep(1);
	MH_Uninitialize();

	if (pOverlayRenderer != nullptr)
		delete pOverlayRenderer;
}

void Hooks::Activate() {
	int mainThread = Tools::GetMainThreadID(GetCurrentProcessId());
	HANDLE mainThreadHandle = OpenThread(THREAD_SUSPEND_RESUME, false, mainThread);
	SuspendThread(mainThreadHandle);

	MH_EnableHook(MH_ALL_HOOKS);

	ResumeThread(mainThreadHandle);
	CloseHandle(mainThreadHandle);
}

#ifdef _WIN64
SIZE_T* __fastcall Hooks::hook_ProcessNewLine(void *pthis, DWORD *dw1, char **dw2, int n) {
#else
SIZE_T* __fastcall Hooks::hook_ProcessNewLine(void *pthis, void *unused, DWORD *dw1, char **dw2, int n) {
#endif
	mHookedFunctionDepth++;
	if (strncmp((char*) dw2[1], "/o:o", 3) == 0) {
		dll->hooks()->GetOverlayRenderer()->mConfig.Show();
	} else if (strncmp((char*) dw2[1], "/o", 3) == 0) {
		if (pOverlayRenderer != nullptr)
			pOverlayRenderer->SetUseDrawOverlay(!pOverlayRenderer->GetUseDrawOverlay());
	}
	std::string k("cmsg");
	chatObject = pthis;
	k.append((char*) dw2[1]);
	dll->pipe()->SendInfo(k);
	SIZE_T *res = pfnBridge.ProcessNewLine(pthis, dw1, dw2, n);
	mHookedFunctionDepth--;
	return res;
}

#ifdef _WIN64
int __fastcall Hooks::hook_OnNewChatItem(void *pthis, PCHATITEM param, size_t n) {
#else
int __fastcall Hooks::hook_OnNewChatItem(void *pthis, void *unused, PCHATITEM param, size_t n) {
#endif
	mHookedFunctionDepth++;
	std::string k("chat");
	k.append((char*) param, n);
	dll->pipe()->SendInfo(k);
	int res = pfnBridge.OnNewChatItem(pthis, param, n);
	mHookedFunctionDepth--;
	return res;
}

int WINAPI Hooks::hook_socket_recv(SOCKET s, char* buf, int len, int flags) {
	mHookedFunctionDepth++;
	DWORD result = 0, flags2 = flags;
	WSABUF buffer = { static_cast<ULONG>(len), buf };

	int alen = WSARecv(s, &buffer, 1, &result, &flags2, nullptr, nullptr) == 0 ? static_cast<int>(result) : SOCKET_ERROR;
	__try {
		if (alen > 0)
			dll->process()->OnRecv(buf, alen);
	} __except (EXCEPTION_EXECUTE_HANDLER) {}
	mHookedFunctionDepth--;
	return alen;
}

int WINAPI Hooks::hook_socket_send(SOCKET s, const char* buf, int len, int flags) {
	mHookedFunctionDepth++;
	DWORD result = 0;
	WSABUF buffer = { static_cast<ULONG>(len), const_cast<CHAR *>(buf) };

	int alen = WSASend(s, &buffer, 1, &result, flags, nullptr, nullptr) == 0 ? static_cast<int>(result) : SOCKET_ERROR;
	__try {
		if (alen > 0)
			dll->process()->OnSent(buf, alen);
	} __except (EXCEPTION_EXECUTE_HANDLER) {}
	mHookedFunctionDepth--;
	return alen;
}

char Hooks::hook_ProcessWindowMessage() {
	mHookedFunctionDepth++;
	std::string chatMessage;
	if (chatObject != 0) {
		if (dll->pipe()->injectQueue.tryPop(&chatMessage)) {
			DWORD res;
			chatPtrs[3] = chatPtrs[1] = (char*) chatMessage.c_str();
			pfnOrig.ProcessNewLine(chatObject, &res, chatPtrs, 20);
		}
	}

#ifdef _WIN64
#else
	if (pDX9Table != 0 && !mHookStarted) {
		mHookStarted = true;
		MH_EnableHook(MH_ALL_HOOKS);
	}
#endif

	if (GetAsyncKeyState(VK_SNAPSHOT) == (SHORT) 0x8001)
		pOverlayRenderer->CaptureScreen();


	char res = pfnBridge.ProcessWindowMessage();
	mHookedFunctionDepth--;
	return res;
}

#ifdef _WIN64

HRESULT APIENTRY Hooks::hook_Dx11SwapChain_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
	mHookedFunctionDepth++;
	if (pOverlayRenderer == nullptr) {
		ID3D11Device *pDevice;
		ID3D11DeviceContext *pContext;
		pSwapChain->GetDevice(__uuidof(ID3D11Device), (LPVOID*) &pDevice);
		pDevice->GetImmediateContext(&pContext);
		pOverlayRenderer = new OverlayRendererDX11(dll, pDevice, pContext, pSwapChain);
	}
	if (pOverlayRenderer != nullptr) {
		pOverlayRenderer->CheckCapture();
		pOverlayRenderer->DrawOverlay();
	}

	HRESULT res = pfnBridge.Dx11SwapChainPresent(pSwapChain, SyncInterval, Flags);
	mHookedFunctionDepth--;
	return res;

}
#else
HRESULT APIENTRY Hooks::hook_Dx9Reset(IDirect3DDevice9 *pDevice, D3DPRESENT_PARAMETERS *pPresentationParameters) {
	mHookedFunctionDepth++;
	if (pOverlayRenderer != nullptr)
		pOverlayRenderer->OnLostDevice();
	HRESULT res = pfnBridge.Dx9Reset(pDevice, pPresentationParameters);
	if (pOverlayRenderer != nullptr)
		pOverlayRenderer->OnResetDevice();
	mHookedFunctionDepth--;
	return res;
}

HRESULT APIENTRY Hooks::hook_Dx9EndScene(IDirect3DDevice9 *pDevice) {
	mHookedFunctionDepth++;

	if (pfnBridge.Dx9SwapChainPresent == nullptr && pDevice->GetNumberOfSwapChains() > 0) {
		IDirect3DSwapChain9 *sc;
		pDevice->GetSwapChain(0, &sc);
		void **vtable = *(void ***) sc;
		MH_CreateHook((LPVOID) vtable[3], hook_Dx9SwapChain_Present, (LPVOID*) &pfnBridge.Dx9SwapChainPresent);
		MH_EnableHook(MH_ALL_HOOKS);
		pOverlayRenderer = new OverlayRendererDX9(dll, pDevice);
	}

	HRESULT res = pfnBridge.Dx9EndScene(pDevice);
	mHookedFunctionDepth--;
	return res;
}

HRESULT APIENTRY Hooks::hook_Dx9SwapChain_Present(IDirect3DSwapChain9 *pSwapChain, const RECT    *pSourceRect, const RECT    *pDestRect, HWND    hDestWindowOverride, const RGNDATA *pDirtyRegion, DWORD dwFlags) {
	mHookedFunctionDepth++;

	if (pOverlayRenderer != nullptr) {
		pOverlayRenderer->CheckCapture();
		pOverlayRenderer->DrawOverlay();
	}

	HRESULT res = pfnBridge.Dx9SwapChainPresent(pSwapChain, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);

	mHookedFunctionDepth--;
	return res;
}

HRESULT APIENTRY Hooks::hook_Dx9Present(IDirect3DDevice9 *pDevice, const RECT    *pSourceRect, const RECT    *pDestRect, HWND    hDestWindowOverride, const RGNDATA *pDirtyRegion) {
	mHookedFunctionDepth++;

	if (pOverlayRenderer != nullptr) {
		pOverlayRenderer->DrawOverlay();
	}

	HRESULT res = pfnBridge.Dx9Present(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);

	mHookedFunctionDepth--;
	return res;
}
#endif

HCURSOR WINAPI Hooks::hook_WinApiSetCursor(HCURSOR hCursor) {
	if (mLockCursor)
		return hCursor;
	else
		return pfnBridge.WinApiSetCursor(hCursor);
}

void Hooks::updateLastFocus(WindowControllerBase *control) {
	if (ffxivHookCaptureControl == control)
		return;
	OverlayRenderer::Control *tmp;
	tmp = ffxivHookCaptureControl;
	while (tmp != nullptr) {
		tmp->statusFlag[CONTROL_STATUS_FOCUS] = 0;
		tmp = tmp->getParent();
	}
	tmp = ffxivHookCaptureControl = control;
	while (tmp != nullptr) {
		tmp->statusFlag[CONTROL_STATUS_FOCUS] = 1;
		tmp = tmp->getParent();
	}
}

LRESULT CALLBACK Hooks::hook_ffxivWndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam) {
	bool callDef = false;
	ImGuiIO &io = ImGui::GetIO();
#ifdef _WIN64
	ImGui_ImplDX11_WndProcHandler(hWnd, iMessage, wParam, lParam);
#else
	ImGui_ImplDX9_WndProcHandler(hWnd, iMessage, wParam, lParam);
#endif
	if (io.WantCaptureMouse)
		mLockCursor = true;
	switch (iMessage) {
		case WM_MOUSEMOVE:
			callDef = true;
		case WM_RBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_LBUTTONDBLCLK:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_MBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MOUSEHOVER:
		case WM_MOUSEHWHEEL:
		case WM_MOUSELEAVE:
		case WM_MOUSEWHEEL:
			if (io.WantCaptureMouse) {
				return 0;
			} else if (pOverlayRenderer != nullptr) {
				int xPos = GET_X_LPARAM(lParam);
				int yPos = GET_Y_LPARAM(lParam);
				WindowControllerBase *pos = pOverlayRenderer->GetWindowAt(xPos, yPos);
				if (lastHover != nullptr)
					lastHover->statusFlag[CONTROL_STATUS_HOVER] = 0;
				lastHover = pos;
				if (lastHover != nullptr)
					lastHover->statusFlag[CONTROL_STATUS_HOVER] = 1;
				if (ffxivHookCaptureControl != nullptr && ffxivHookCaptureControl->isLocked())
					updateLastFocus(nullptr);
				WindowControllerBase *control = ffxivHookCaptureControl ? ffxivHookCaptureControl : pos;
				if (control && !ffxivWndPressed) {
					mLockCursor = true;
					int res = control->callback(hWnd, iMessage, wParam, lParam);
					switch (res) {
						case 0: updateLastFocus(nullptr); callDef = true; break;
						case 1: updateLastFocus(nullptr); callDef = false; break;
						case 2: updateLastFocus(control); callDef = true; break;
						case 3: updateLastFocus(control); callDef = false; break;
						case 4: callDef = true; break;
						case 5: callDef = false; break;
					}
					if (pos != control && pos != nullptr) {
						int res = pos->callback(hWnd, iMessage, wParam, lParam);
						switch (res) {
							case 0: updateLastFocus(nullptr); callDef = true; break;
							case 1: updateLastFocus(nullptr); callDef = false; break;
							case 2: updateLastFocus(pos); callDef = true; break;
							case 3: updateLastFocus(pos); callDef = false; break;
							case 4: callDef = true; break;
							case 5: callDef = false; break;
						}
					}
				} else {
					mLockCursor = false;
					callDef = true;
					switch (iMessage) {
						case WM_LBUTTONDOWN:
						case WM_RBUTTONDOWN:
						case WM_MBUTTONDOWN:
							ffxivWndPressed = true;
							break;
						case WM_LBUTTONUP:
						case WM_RBUTTONUP:
						case WM_MBUTTONUP:
							ffxivWndPressed = false;
							break;
					}
				}
			} else
				callDef = true;
			break;
		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_CHAR:
			if (io.WantCaptureKeyboard || io.WantTextInput)
				return 0;
			else if (ffxivHookCaptureControl && !ffxivWndPressed) {
				int res = ffxivHookCaptureControl->callback(hWnd, iMessage, wParam, lParam);
				switch (res) {
					case 0: updateLastFocus(nullptr); callDef = true; break;
					case 1: updateLastFocus(nullptr); callDef = false; break;
					case 2: updateLastFocus(ffxivHookCaptureControl); callDef = true; break;
					case 3: updateLastFocus(ffxivHookCaptureControl); callDef = false; break;
					case 4: callDef = true; break;
					case 5: callDef = false; break;
				}
			} else
				callDef = true;
			break;
		default:
			callDef = true;
			if (ffxivHookCaptureControl && !ffxivWndPressed) {
				WindowControllerBase *control = ffxivHookCaptureControl;
				int res = control->callback(hWnd, iMessage, wParam, lParam);
				switch (res) {
					case 0: updateLastFocus(nullptr); callDef = true; break;
					case 1: updateLastFocus(nullptr); callDef = false; break;
					case 2: updateLastFocus(control); callDef = true; break;
					case 3: updateLastFocus(control); callDef = false; break;
					case 4: callDef = true; break;
					case 5: callDef = false; break;
				}
			}
	}
	if (callDef)
		return CallWindowProc(ffxivWndProc, hWnd, iMessage, wParam, lParam);
	return 0;
}