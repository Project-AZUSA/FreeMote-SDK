#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <d3dx9.h>
#include <vector>
#include "iemote.h"

#pragma comment(lib,"winmm.lib")
#pragma comment(lib,"d3d9.lib")
#pragma comment(lib,"d3dx9.lib")
#pragma comment(lib,"emotedriver.lib")

#define USE_RENDERTARGET 1

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#define SAFE_RELEASE(p) { if(p){ (p)->Release(); (p)=NULL; } }
#define FVF_LVERTEX     (D3DFVF_XYZ    | D3DFVF_DIFFUSE | D3DFVF_TEX1)

struct LVERTEX {
  float    x, y, z;
  D3DCOLOR color;
  float    tu, tv;
};

//------------------------------------------------
// 定数定義
//------------------------------------------------

//------------------------------------------------
// 変換
static const float MSTOF60THS = 60.0f / 1000.0f;  // msを1/60秒カウントへ変換。
static const float F60THSTOMS = 1000.0f / 60.0f;  // 1/60秒カウントをmsへ変換。

//------------------------------------------------
// 画面サイズ
static const int SCREEN_WIDTH = 1280;
static const int SCREEN_HEIGHT = 720;
int sScreenWidth = SCREEN_WIDTH;
int sScreenHeight = SCREEN_HEIGHT;

//------------------------------------------------
// E-moteデータファイル
static const wchar_t *MOTION_DATA_PATH = L"emote_test.pure.psb";

//------------------------------------------------
// Window関連
HWND sHwnd;
int sPrevMouseX, sPrevMouseY;
int sLeftMouseDragging;
int sRightMouseDragging;
std::vector<wchar_t*> sDroppedFiles;

//------------------------------------------------
// D3D関連
LPDIRECT3D9 sD3D;
D3DCAPS9 sD3DCaps;
D3DPRESENT_PARAMETERS sD3Dpp;
LPDIRECT3DDEVICE9 sD3DDevice;
LPDIRECT3DTEXTURE9 sCanvasTexture;
int sCanvasTextureWidth, sCanvasTextureHeight;
LPDIRECT3DSURFACE9 sBackBufferSurface;

//------------------------------------------------
// Emote関連
IEmoteDevice *sEmoteDevice;
IEmotePlayer *sEmotePlayer;
std::vector<IEmotePlayer *> sClonePlayerList;
int sPoseIndex, sMouthIndex;

//------------------------------------------------
// 関数宣言
//------------------------------------------------
/*------------------------------
 * 2の累乗への切り上げ
 ------------------------------*/
static DWORD clp(DWORD x) {
  x = x - 1;
  x = x | (x >> 1);
  x = x | (x >> 2);
  x = x | (x >> 4);
  x = x | (x >> 8);
  x = x | (x >> 16);
  return x + 1;
}

struct VariableTable
{
  const char *key;
  float value;
};

bool WindowInit(HINSTANCE hInstance, int nWinMode);
bool WindowMessage(void);

LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam);

static void D3DInit(void);
static void D3DInitRenderState(void);

static void EmoteInit(void);
static void EmoteNewPlayer(int numFiles, const wchar_t **filenames);
static void EmoteDeletePlayer(void);
static void EmoteDropFiles(HDROP hDrop);
static void EmoteInvalidateDroppedFiles(void);
static void EmoteUpdate(float ms);
static void EmoteDraw(void);
static void EmoteOffsetCoord(int ofsx, int ofsy);
static void EmoteOffsetScale(float ofstScale);
static void EmoteOffsetRot(float ofstRot);
static void EmoteSetVariables(const VariableTable *table, float time, float easing);
static void EmoteUpdatePoseTimeline(int newIndex);
static void EmoteUpdateMouthTimeline(int newIndex);
static void EmoteSwitchMouth(void);
static void EmoteSkip(void);
static void EmoteNew(void);
static void EmoteDelete(void);

//------------------------------------------------
// 変数設定のセット
//------------------------------------------------
static const char *sPoseTimelineList[] = {
  "test00",
  "test01",
  "test02",
  "test03"
};

static const char *sMouthTimelineList[] = {
  "mouth_test00",
  "mouth_test01"
};

static const VariableTable
sFaceTable0[] =
  {
    { "face_eye_UD", 0.0 },
    { "face_eye_LR", 0.0 },
    { "face_eye_open", 0.0 },
    { "face_eyebrow", 0.0 },
    { "face_mouth", 0.0 },
    { "face_talk", 0.0 },
    { "face_tears", 0.0 },
    { "face_cheek", 0.0} ,
    {}
    };

static const VariableTable
sFaceTable1[] =
  {
    { "face_eye_UD", 0.0 },
    { "face_eye_LR", -30 },
    { "face_eye_open", 0.0 },
    { "face_eyebrow", 40 },
    { "face_mouth", 30 },
    { "face_talk", 0.0 },
    { "face_tears", 0.0 },
    { "face_cheek", 1 },
    {},
    };

static const VariableTable
sFaceTable2[] =
  {
    { "face_eye_UD", 0.0 },
    { "face_eye_LR", -30.0 },
    { "face_eye_open", 10 },
    { "face_eyebrow",  30 },
    { "face_mouth", 20 },
    { "face_talk", 0.0 },
    { "face_tears", 1 },
    { "face_cheek", 0.0 },
    {},
    };

static const VariableTable
sFaceTable3[] =
  {
    { "face_eye_UD", -30 },
    { "face_eye_LR", 0.0 },
    { "face_eye_open", 5 },
    { "face_eyebrow", 20 },
    { "face_mouth", 20 },
    { "face_talk", 0.5 },
    { "face_tears", 0.0 },
    { "face_cheek", 0.0 },
    {},
    };


//------------------------------------------------
// メイン
//------------------------------------------------
void
RequireCanvasTexture(void)
{
  if (sCanvasTexture)
    return;

  sCanvasTextureWidth = clp(SCREEN_WIDTH);
  sCanvasTextureHeight = clp(SCREEN_HEIGHT);
  HRESULT hr;
  hr = sD3DDevice->CreateTexture(sCanvasTextureWidth, sCanvasTextureHeight,
                                 1,
                                 D3DUSAGE_RENDERTARGET,
                                 D3DFMT_A8R8G8B8,
                                 D3DPOOL_DEFAULT,
                                 &sCanvasTexture,
                                 NULL);
}

#if USE_RENDERTARGET
void
AttachCanvasTexture(void)
{
  RequireCanvasTexture();

  if (! sCanvasTexture)
    return;

  // バックバッファサーフェース退避
  sD3DDevice->GetRenderTarget(0, &sBackBufferSurface);
  // レンダターゲットを設定
  LPDIRECT3DSURFACE9 surface;
  sCanvasTexture->GetSurfaceLevel(0, &surface);
  sD3DDevice->SetRenderTarget(0, surface);
  surface->Release();
  // ビューポート設定
  D3DVIEWPORT9 vp = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0.0f, 1.0f };
  sD3DDevice->SetViewport(&vp);
  sD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255,255,255,255) , 1.0f, 0);
}

void
DetachCanvasTexture(void)
{
  if (! sCanvasTexture)
    return;

  // バックバッファサーフェース復帰
  sD3DDevice->SetRenderTarget(0, sBackBufferSurface);
  sBackBufferSurface->Release();
}

void
DrawCanvasTexture(void)
{
  if (! sCanvasTexture)
    return;

  float vl = -SCREEN_WIDTH / 2;
  float vt = -SCREEN_HEIGHT / 2;
  float vr = SCREEN_WIDTH / 2;
  float vb = SCREEN_HEIGHT / 2;
  float tl = 0.0f;
  float tt = 0.0f;
  float tr = float(SCREEN_WIDTH) / sCanvasTextureWidth;
  float tb = float(SCREEN_HEIGHT) / sCanvasTextureHeight;

  LVERTEX vtx[4] = {
    { vl, vt, 0, D3DCOLOR_ARGB(255, 255, 255, 255), tl, tt },
    { vr, vt, 0, D3DCOLOR_ARGB(255, 255, 255, 255), tr, tt },
    { vl, vb, 0, D3DCOLOR_ARGB(255, 255, 255, 255), tl, tb },
    { vr, vb, 0, D3DCOLOR_ARGB(255, 255, 255, 255), tr, tb }
  };

  sD3DDevice->SetTexture(0, sCanvasTexture);
  sD3DDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vtx, sizeof(LVERTEX));
  sD3DDevice->SetTexture(0, NULL);
}
#endif

int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE hPrevInst,LPSTR lpszArgs,int nWinMode)
{
#ifdef _DEBUG
  _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

  // 初期化
  if (! WindowInit(hInstance, nWinMode))
    return 0;

  D3DInit();
  EmoteInit();

  timeBeginPeriod(1);
  DWORD time = timeGetTime();
  float wait = 0;
  float processedTime = 0;

  // メインループ
  while(true){
    // メッセージ処理
    if (! WindowMessage())
      break;

    // フレーム更新処理
    EmoteUpdate(processedTime);

    // 描画処理
    sD3DDevice->BeginScene();
    sD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0,0,0,0) , 1.0f, 0);

    // キャンバステクスチャをアタッチ
#if USE_RENDERTARGET
    AttachCanvasTexture();
#endif
    // 描画実行
    EmoteDraw();
    // キャンバステクスチャをデタッチ
#if USE_RENDERTARGET
    DetachCanvasTexture();
    // キャンバステクスチャをバックバッファへ描画
    DrawCanvasTexture();
#endif
    // 描画完了
    sD3DDevice->EndScene();
    HRESULT hr = sD3DDevice->Present(NULL, NULL, NULL, NULL);

    // デバイスロストから復帰
    if(FAILED(hr)){
      if (hr == D3DERR_DEVICELOST) {
        // デバイスロストはE-moteデバイスにも通知して、適切にリソースを解放させる
        // (デバイスロスト中にE-moteの描画処理を続けて呼んでも問題は無い）
        sEmoteDevice->OnDeviceLost();
        SAFE_RELEASE(sCanvasTexture);
        // 準備が整ったらデバイスをリセット
        hr = sD3DDevice->TestCooperativeLevel();
        if (hr == D3DERR_DEVICELOST)
          ;
        else if (hr == D3DERR_DEVICENOTRESET) {
          sD3DDevice->Reset(&sD3Dpp);
          D3DInitRenderState();
        }
      }
    }

    // 1/60秒経過するまでウエイト。
    processedTime = 0;
    wait = F60THSTOMS;

    for(;;){
      DWORD t = timeGetTime();
      wait -=  (t - time);
      processedTime += (t - time);
      time = t;
      if (wait <= 0)
        break;
      Sleep(1);
    }
  }

  for (emote_uint32_t i = 0; i < sClonePlayerList.size(); i++)
    sClonePlayerList[i]->Release();
  sEmotePlayer->Release();
  sEmoteDevice->Release();

  return 0;
}

//------------------------------------------------
// メモリ確保/解放(メインメモリの管理オブジェクト用)
//------------------------------------------------

//------------------------------------------------
// メモリ確保
void *
ObjAlloc(std::size_t size)
{
  // 独自のalloc関数をここに配置する
  OutputDebugString(L"ObjAlloc\n");
  return malloc(size);
}

//------------------------------------------------
// メモリ解放
void
ObjFree(void * ptr)
{
  // 独自のfree関数をここに配置する
  OutputDebugString(L"ObjFree\n");
  free(ptr);
}

//------------------------------------------------
// Window処理
//------------------------------------------------

//------------------------------------------------
// Window初期化
bool
WindowInit(HINSTANCE hInstance, int nWinMode)
{
  WNDCLASSEX wcl;
  ZeroMemory(&wcl,sizeof(wcl));
  wcl.hInstance = hInstance;
  wcl.lpszClassName = L"EmoteDriverSample";
  wcl.lpfnWndProc = WindowProc;
  wcl.style = 0;
  wcl.cbSize = sizeof(WNDCLASSEX);
  wcl.hIcon = LoadIcon(NULL,IDI_APPLICATION);
  wcl.hCursor = LoadCursor(NULL,IDC_ARROW);
  wcl.lpszMenuName = NULL;
  wcl.cbClsExtra = 0;
  wcl.cbWndExtra = 0;
  wcl.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  if(!RegisterClassEx(&wcl)){
    return false;
  }

  int winWidth, winHeight;
  winWidth = sScreenWidth + GetSystemMetrics(SM_CXDLGFRAME) * 2;
  winHeight = sScreenHeight + GetSystemMetrics(SM_CYDLGFRAME) * 2 + GetSystemMetrics(SM_CYCAPTION);

  sHwnd = CreateWindow(L"EmoteDriverSample",
                       L"Emote Driver Sample",
                       WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
                       100,
                       100,
                       winWidth,
                       winHeight,
                       NULL,
                       NULL,
                       hInstance,
                       NULL

                       );
  UpdateWindow(sHwnd);
  DragAcceptFiles(sHwnd, TRUE);

  return true;
}

//------------------------------------------------
// Windowメッセージ処理
bool
WindowMessage(void)
{
  MSG msg;

  // メッセージキューに溜まっているメッセージを全て処理
  while (PeekMessage(&msg,NULL,0,0,PM_NOREMOVE)) {
    if(GetMessage (&msg,NULL,0,0)){
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else {
      // QUITメッセージを受信したらfalseを返す
      return false;
    }
  }
  return true;
}

//------------------------------------------------
// Windowメッセージコールバック
LRESULT CALLBACK WindowProc(HWND hwnd,UINT message, WPARAM wParam, LPARAM lParam)
{
  switch(message){
  case WM_KEYDOWN:
    switch(wParam){
      // ESCが押されたら終了
    case VK_ESCAPE:
      PostMessage(hwnd,WM_CLOSE,0,0);
      break;
      // キー入力に応じてE-moteプレイやへ動作指示
    case 'Q': EmoteUpdatePoseTimeline(0); break;
    case 'W': EmoteUpdatePoseTimeline(1); break;
    case 'E': EmoteUpdatePoseTimeline(2); break;
    case 'R': EmoteUpdatePoseTimeline(3); break;
    case 'A': EmoteSetVariables(sFaceTable0, 150, 0); break;
    case 'S': EmoteSetVariables(sFaceTable1, 150, 0); break;
    case 'D': EmoteSetVariables(sFaceTable2, 150, 0); break;
    case 'F': EmoteSetVariables(sFaceTable3, 150, 0); break;
    case 'Z': EmoteSwitchMouth(); break;
    case 'X': EmoteSkip(); break;
    case '1': EmoteNew(); break;
    case '2': EmoteDelete(); break;
    case VK_RETURN: EmoteInvalidateDroppedFiles(); break;
    }
    break;

  // マウス左ボタンドラッグ開始
  case WM_LBUTTONDOWN:
    sLeftMouseDragging = true;
    SetCapture(sHwnd);
    break;

  // マウス左ボタンドラッグ終了
  case WM_LBUTTONUP:
    sLeftMouseDragging = false;
    if (! sRightMouseDragging)
      ReleaseCapture();
    break;

  // マウス右ボタンドラッグ開始
  case WM_RBUTTONDOWN:
    sRightMouseDragging = true;
    SetCapture(sHwnd);
    break;

  // マウス右ボタンドラッグ終了
  case WM_RBUTTONUP:
    sRightMouseDragging = false;
    if (! sLeftMouseDragging)
      ReleaseCapture();
    break;

  // マウス移動
  case WM_MOUSEMOVE: {
    int x, y;
    x =  GET_X_LPARAM(lParam);
    y =  GET_Y_LPARAM(lParam);
    // 左マウス押してたら座標オフセット処理
    if (sLeftMouseDragging) {
      EmoteOffsetCoord(x - sPrevMouseX, y - sPrevMouseY);
    }
    // 右マウス押してたら回転オフセット処理
    if (sRightMouseDragging) {
      float cx, cy;
      sEmotePlayer->GetCoord(cx, cy);
      cx += SCREEN_WIDTH / 2;
      cy += SCREEN_HEIGHT / 2;
      float ax(x - cx), ay(y - cy), bx(sPrevMouseX - cx), by(sPrevMouseY - cy);
      EmoteOffsetRot(atan2f(ay, ax) - atan2f(by, bx));
    }
    sPrevMouseX = x;
    sPrevMouseY = y;
    break;
  }

  // ホイール処理
  case WM_MOUSEWHEEL: {
    // スケールのオフセット処理
    float w = 1.0f * GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
    if (w > 0)
      w = powf(1.1f, w);
    else
      w = powf(1 / 1.1f, -w);

    EmoteOffsetScale(w);
    break;
  }

  // ファイルドロップ
  case WM_DROPFILES: {
    EmoteDropFiles((HDROP)wParam);
    break;
  }

  case WM_DESTROY:
    PostQuitMessage(0);
    break;

  case WM_SIZE:
	sScreenWidth = LOWORD(lParam);
    sScreenHeight = HIWORD(lParam);
    D3DInitRenderState();
    break;

  default:
    return DefWindowProc(hwnd,message,wParam,lParam);
  }

  return 0;
}

//------------------------------------------------
// D3D制御
//------------------------------------------------

//------------------------------------------------
// レンダーステートを初期化
void
D3DInitRenderState(void)
{
  if (sD3DDevice == NULL)
    return;

  D3DXMATRIX matWorld;
  D3DXMatrixIdentity(&matWorld);

  D3DXMATRIX matProj;
  D3DXMatrixIdentity(&matProj);

  float aspect = 1.0f * sScreenWidth / sScreenHeight;
  matProj._11 = 1.0f / aspect;
  matProj._34 = 1.0f;
  matProj._44 = 0.0f;
  matProj._41 = 0.0f;
  matProj._42 = 0.0f;

  float scale = 1.0f;
  float fw = SCREEN_WIDTH;
  float fh = SCREEN_HEIGHT;

  D3DXMATRIX matView;
  D3DXMatrixLookAtLH(&matView,
                     &D3DXVECTOR3(-0.5f,0.5f,-fh/2),
                     &D3DXVECTOR3(-0.5f,0.5f,0),
                     &D3DXVECTOR3(0,-1,0));

  matView._11 = -matView._11;

  // E-moteドライバは、以下3つの行列値に応じて射影を行う。
  sD3DDevice->SetTransform(D3DTS_WORLD, &matWorld);
  sD3DDevice->SetTransform(D3DTS_VIEW, &matView);
  sD3DDevice->SetTransform(D3DTS_PROJECTION, &matProj);

  // レンダリングターゲット描画用に最低限のパラメータ初期化
  sD3DDevice->SetFVF(FVF_LVERTEX);
  sD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
  sD3DDevice->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
  sD3DDevice->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
  sD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
  sD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
  sD3DDevice->SetRenderState(D3DRS_BLENDOP  ,D3DBLENDOP_ADD);
  sD3DDevice->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
  sD3DDevice->SetRenderState(D3DRS_SRCBLEND ,D3DBLEND_SRCALPHA);
  sD3DDevice->SetRenderState(D3DRS_BLENDOPALPHA  , D3DBLENDOP_ADD);
  sD3DDevice->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE);
  sD3DDevice->SetRenderState(D3DRS_SRCBLENDALPHA , D3DBLEND_ONE);
  sD3DDevice->SetRenderState(D3DRS_LIGHTING,FALSE);
  sD3DDevice->SetRenderState( D3DRS_ZENABLE, FALSE );
  sD3DDevice->SetRenderState(D3DRS_ALPHAREF, 0x00);
  sD3DDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
  sD3DDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
  sD3DDevice->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
  sD3DDevice->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
  sD3DDevice->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);
  sD3DDevice->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
  sD3DDevice->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
  sD3DDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
  sD3DDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  sD3DDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
  sD3DDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
}

//------------------------------------------------
// D3Dを初期化
void
D3DInit(void)
{
  // D3D
  sD3D = Direct3DCreate9(D3D_SDK_VERSION);

  HRESULT hr;
  hr = sD3D->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &sD3DCaps);

  memset(&sD3Dpp,0,sizeof(sD3Dpp));
  sD3Dpp.BackBufferWidth		= SCREEN_WIDTH;
  sD3Dpp.BackBufferHeight		= SCREEN_HEIGHT;
  sD3Dpp.BackBufferFormat		= D3DFMT_UNKNOWN;
  sD3Dpp.Windowed				= true;
  sD3Dpp.SwapEffect				= D3DSWAPEFFECT_DISCARD;
  sD3Dpp.PresentationInterval	= D3DPRESENT_INTERVAL_IMMEDIATE;

  hr = sD3D->CreateDevice(D3DADAPTER_DEFAULT,
                          D3DDEVTYPE_HAL,
                          sHwnd,
                          D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_HARDWARE_VERTEXPROCESSING,
                          &sD3Dpp,
                          &sD3DDevice);

  D3DInitRenderState();
}


//------------------------------------------------
// E-mote制御
//------------------------------------------------

//------------------------------------------------
// グレイスケールフィルタ
void GrayscaleFilter(BYTE *image, ULONG imageSize)
{
  while (imageSize) {
    BYTE gray = int(0.298912f * image[2] + 0.586611f * image[1] + 0.114478f * image[0]);
    image[0] = image[1] = image[2] = gray;
    image += 4;
    imageSize -= 4;
  }
}

//------------------------------------------------
// 初期化
void EmoteInit(void)
{
  // E-moteデバイス作成
  IEmoteDevice::InitParam param;
  memset(&param, 0, sizeof(param));
  param.d3dDevice = sD3DDevice;
  // メモリ確保・解放関数の指定はNULLのままならデフォルトのmalloc/freeが使われる
  // param.objAllocator.alloc = &ObjAlloc;
  // param.objAllocator.free =  &ObjFree;
  sEmoteDevice = EmoteCreate(param);
//  sEmoteDevice->SetMaskRegionClipping(true);
  sEmoteDevice->SetShaderModel(IEmoteDevice::SHADER_MODEL_2);

  // E-mtoeデータファイル読み込み
  EmoteNewPlayer(1, &MOTION_DATA_PATH);
}

//------------------------------------------------
// プレイヤ初期化
void EmoteNewPlayer(int numFiles, const wchar_t **filenames)
{
  emote_uint32_t emoteObjectSize[256];
  emote_uint8_t *emoteObjectImage[256];

  // E-mtoeデータファイル読み込み
  for (int i = 0; i < numFiles; i++) {
    HANDLE handle = CreateFile(filenames[i],
                               GENERIC_READ,
                               0,
                               NULL,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);

    emoteObjectSize[i] = GetFileSize(handle, NULL);
    emoteObjectImage[i] = new BYTE[emoteObjectSize[i]];
    DWORD dummy;
    ReadFile(handle, emoteObjectImage[i], emoteObjectSize[i], &dummy, NULL);
    CloseHandle(handle);

#if 0
    // E-moteファイルイメージに事前にフィルタ処理を行う
    EmoteFilterTexture(emoteObjectImage[i], emoteObjectSize[i], GrayscaleFilter);
#endif
  }

  // プレイヤを作成する
  sEmoteDevice->CreatePlayer(numFiles, (const emote_uint8_t **)&emoteObjectImage, (const emote_uint32_t *)&emoteObjectSize, &sEmotePlayer);

  // 処理の終わったファイルイメージを破棄
  for (int i = 0; i < numFiles; i++)
    delete[] emoteObjectImage[i];

  // 表示開始
  sEmotePlayer->Show();
  sEmotePlayer->SetVariable("fade_z", 1);
}

//------------------------------------------------
// プレイヤ破棄
void EmoteDeletePlayer(void)
{
  if (sEmotePlayer)
    sEmotePlayer->Release();
  sEmotePlayer = NULL;

  while (! sClonePlayerList.empty())
    EmoteDeletePlayer();
}

//------------------------------------------------
// ファイルドロップ
void EmoteDropFiles(HDROP hDrop)
{
  UINT numFiles;

  numFiles = DragQueryFile(hDrop,-1,NULL,0);

  for (UINT i = 0; i < numFiles; i++) {
    wchar_t *filename = new wchar_t[MAX_PATH + 1];
    DragQueryFile(hDrop, i, filename, MAX_PATH);
    sDroppedFiles.push_back(filename);
  }

  DragFinish(hDrop);
}


//------------------------------------------------
// ファイルドロップ
void EmoteInvalidateDroppedFiles(void)
{
  if (sDroppedFiles.empty())
    return;

  EmoteDeletePlayer();
  EmoteNewPlayer(sDroppedFiles.size(), (const wchar_t**)&sDroppedFiles[0]);

  for (UINT i = 0; i < sDroppedFiles.size(); i++) {
    delete[] sDroppedFiles[i];
  }
  sDroppedFiles.clear();
}

//------------------------------------------------
// フレーム更新
void EmoteUpdate(float ms)
{
  // E-moteは1/60秒を1単位で駆動するので時間単位を変換。
  sEmotePlayer->Progress(ms * MSTOF60THS);
  for (emote_uint32_t i = 0; i < sClonePlayerList.size(); i++)
    sClonePlayerList[i]->Progress(ms * MSTOF60THS);
}

//------------------------------------------------
// 描画
void EmoteDraw(void)
{
  sEmoteDevice->OnRenderTarget(sCanvasTexture);
  sEmotePlayer->Render();
  for (emote_uint32_t i = 0; i < sClonePlayerList.size(); i++) {
    sEmoteDevice->OnRenderTarget(sCanvasTexture);
    sClonePlayerList[i]->Render();
  }
}

//------------------------------------------------
// 座標のオフセット処理
void
EmoteOffsetCoord(int ofsx, int ofsy)
{
  float x, y;
  sEmotePlayer->GetCoord(x, y);
  sEmotePlayer->SetCoord(x + ofsx, y + ofsy);
}

//------------------------------------------------
// スケールのオフセット処理
void EmoteOffsetScale(float ofstScale)
{
  float scale;
  scale = sEmotePlayer->GetScale();
  sEmotePlayer->SetScale(scale * ofstScale);
}

//------------------------------------------------
// 回転のオフセット処理
void EmoteOffsetRot(float ofstRot)
{
  float rot;
  rot = sEmotePlayer->GetRot();
  sEmotePlayer->SetRot(rot + ofstRot);
}

//------------------------------------------------
// 変数の値更新
void
EmoteSetVariables(const VariableTable *table, float time, float easing)
{
  while (table->key) {
    // E-moteは1/60秒を1単位で駆動するので時間単位を変換。
    sEmotePlayer->SetVariable(table->key, table->value, time * MSTOF60THS, easing);
    table++;
  }
  OutputDebugString(L"emote update variables.\n");
}

//------------------------------------------------
// ポーズのタイムラインを更新
void
EmoteUpdatePoseTimeline(int newIndex)
{
  sEmotePlayer->StopTimeline(sPoseTimelineList[sPoseIndex]);
  sPoseIndex = newIndex;
  sEmotePlayer->PlayTimeline(sPoseTimelineList[sPoseIndex], IEmotePlayer::TIMELINE_PLAY_PARALLEL);
  OutputDebugString(L"emote update pose timeline.\n");
}

//------------------------------------------------
// 口のタイムラインを更新
void
EmoteUpdateMouthTimeline(int newIndex)
{
  sEmotePlayer->StopTimeline(sMouthTimelineList[sMouthIndex]);
  sMouthIndex = newIndex;
  sEmotePlayer->PlayTimeline(sMouthTimelineList[sMouthIndex], IEmotePlayer::TIMELINE_PLAY_PARALLEL);
  OutputDebugString(L"emote update pose timeline.\n");
}

//------------------------------------------------
// 口のタイムラインを切り替え
void
EmoteSwitchMouth(void)
{
  EmoteUpdateMouthTimeline((sMouthIndex + 1) % 2);
}

//------------------------------------------------
// スキップ処理
void
EmoteSkip(void)
{
  sEmotePlayer->Skip();
  OutputDebugString(L"emote skip.\n");
}

void
EmoteNew(void)
{
  sClonePlayerList.push_back(sEmotePlayer->Clone());
}

void
EmoteDelete(void)
{
  if (sClonePlayerList.size()) {
    sClonePlayerList[0]->Release();
    sClonePlayerList.erase(sClonePlayerList.begin());
  }
}
