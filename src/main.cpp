#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include <wingdi.h>
#include "process_manager.hpp"
#include "feature_controller.hpp"
#include <string>
#include <vector>
#include <algorithm>
#include <cwchar>
#include <cmath>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "Msimg32.lib")

namespace {
constexpr COLORREF BG = RGB(10, 11, 13);
constexpr COLORREF PANEL = RGB(16, 17, 19);
constexpr COLORREF BORDER = RGB(42, 44, 47);
constexpr COLORREF TEXT = RGB(220, 221, 223);
constexpr COLORREF MUTED = RGB(135, 137, 141);
constexpr COLORREF RED = RGB(245, 246, 248);
struct Option { std::wstring name; bool checked = false; RECT box{}; RECT row{}; bool valueField = false; bool requiresPersonalScript = false; bool uiOnly = false; bool child = false; int parentIndex = -1; bool dataField = false; uintptr_t dataOffset = 0; size_t dataSize = 0; uint64_t numericValue = 0; bool valueAvailable = false; };
struct Category { std::wstring name; std::wstring leftTitle; std::wstring rightTitle; std::vector<Option> options; };
std::vector<Category> categories;
int activeCategory = 0;
std::vector<RECT> navRects;
RECT closeRect{};
RECT minimizeRect{};
ProcessManager processManager;
FeatureController featureController(processManager);
GameConnectionSnapshot lastUiConnection{};
HFONT font = nullptr, optionFont = nullptr, compactOptionFont = nullptr, tinyOptionFont = nullptr, microOptionFont = nullptr, boldFont = nullptr;
ULONG_PTR gdiplusToken = 0;
Gdiplus::Bitmap* navIcons[5]{};
int hoveredElement = -1;
bool trackingMouseLeave = false;
bool timerPrecisionActive = false;
bool squishAnimating = false;
bool scriptRevealAnimating = false;
ULONGLONG scriptRevealStart = 0;
int squishCategory = -1;
bool squishOptionMode = false;
int squishOptionCategory = -1;
int squishOptionIndex = -1;
ULONGLONG squishStartTime = 0;
HDC squishBufferDC = nullptr;
HBITMAP squishBufferBitmap = nullptr;
HBITMAP squishPreviousBitmap = nullptr;
HBITMAP panelSquishFrames[2]{};
RECT panelSquishRects[2]{};
HWND activeValueEditor = nullptr;
int activeValueOption = -1;
bool activeValueIsCharacter = false;
int scrollOffsets[8][2]{};
RECT currentPanelRects[2]{};

Gdiplus::Color gdiColor(COLORREF color);

Gdiplus::Color gdiColor(COLORREF color) {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}
void clearPanelSquish() {
    for (auto& frame : panelSquishFrames) {
        if (frame) { DeleteObject(frame); frame=nullptr; }
    }
}
HBITMAP captureRegion(HWND hwnd, RECT rect) {
    HDC screen=GetDC(hwnd);
    if (!screen) return nullptr;
    HDC memory=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,rect.right-rect.left,rect.bottom-rect.top);
    HBITMAP old=static_cast<HBITMAP>(SelectObject(memory,bitmap));
    BitBlt(memory,0,0,rect.right-rect.left,rect.bottom-rect.top,screen,rect.left,rect.top,SRCCOPY);
    SelectObject(memory,old); DeleteDC(memory); ReleaseDC(hwnd,screen);
    return bitmap;
}
void capturePanelFrames(HWND hwnd) {
    clearPanelSquish();
    RECT client{}; GetClientRect(hwnd,&client);
    const int width=static_cast<int>(client.right), height=static_cast<int>(client.bottom);
    const int contentWidth=(std::min)(600,width-40), gap=12;
    const int panelWidth=(contentWidth-gap)/2;
    const int panelHeight=(std::min)(280,(std::max)(220,height-150));
    const int contentX=(width-contentWidth)/2, top=64;
    panelSquishRects[0]={contentX,top,contentX+panelWidth,top+panelHeight};
    panelSquishRects[1]={contentX+panelWidth+gap,top,contentX+contentWidth,top+panelHeight};
    panelSquishFrames[0]=captureRegion(hwnd,panelSquishRects[0]);
    panelSquishFrames[1]=captureRegion(hwnd,panelSquishRects[1]);
}
void rounded(HDC dc, RECT r, int radius, COLORREF fill, COLORREF stroke) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float x=static_cast<float>(r.left), y=static_cast<float>(r.top);
    const float w=static_cast<float>(r.right-r.left), h=static_cast<float>(r.bottom-r.top);
    const int rectWidth=static_cast<int>(r.right-r.left), rectHeight=static_cast<int>(r.bottom-r.top);
    const float d=static_cast<float>((std::min)(radius*2,(std::min)(rectWidth,rectHeight)));
    Gdiplus::GraphicsPath path;
    path.AddArc(x,y,d,d,180.0f,90.0f); path.AddArc(x+w-d,y,d,d,270.0f,90.0f);
    path.AddArc(x+w-d,y+h-d,d,d,0.0f,90.0f); path.AddArc(x,y+h-d,d,d,90.0f,90.0f); path.CloseFigure();
    Gdiplus::SolidBrush brush(gdiColor(fill)); Gdiplus::Pen pen(gdiColor(stroke),1.0f);
    graphics.FillPath(&brush,&path); graphics.DrawPath(&pen,&path);
}
void label(HDC dc, int x, int y, const std::wstring& s, COLORREF color, HFONT f = nullptr) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::Font textFont(dc, f ? f : font);
    Gdiplus::SolidBrush brush(gdiColor(color));
    graphics.DrawString(s.c_str(), static_cast<INT>(s.size()), &textFont,
        Gdiplus::PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y)), &brush);
}
void drawCheckBox(HDC dc, RECT box, bool checked, float pulse, bool enabled=true) {
    const float sx=1.0f-0.20f*pulse, sy=1.0f-0.25f*pulse;
    const float cx=(box.left+box.right)*0.5f, cy=(box.top+box.bottom)*0.5f;
    RECT scaled{static_cast<LONG>(cx-(box.right-box.left)*0.5f*sx),
        static_cast<LONG>(cy-(box.bottom-box.top)*0.5f*sy),
        static_cast<LONG>(cx+(box.right-box.left)*0.5f*sx),
        static_cast<LONG>(cy+(box.bottom-box.top)*0.5f*sy)};
    const COLORREF fill=!enabled?RGB(27,28,30):(checked?RED:RGB(35,36,39));
    const COLORREF stroke=!enabled?RGB(43,44,47):(checked?RED:RGB(75,77,81));
    rounded(dc,scaled,8,fill,stroke);
    if (checked && enabled) {
        Gdiplus::Graphics graphics(dc); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(Gdiplus::Color(255,255,255,255),2.0f);
        pen.SetStartCap(Gdiplus::LineCapRound); pen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::PointF mark[]={{cx-5.5f*sx,cy-0.5f*sy},{cx-1.5f*sx,cy+3.5f*sy},{cx+5.5f*sx,cy-4.5f*sy}};
        graphics.DrawLines(&pen,mark,3);
    }
}
const std::vector<std::wstring>& playerCharacterNames() {
    static const std::vector<std::wstring> names={L"Eren",L"Mikasa",L"Armin",L"Reiner",L"Bertolt",L"Annie",L"Jean",L"Conny",L"Sasha",L"Christa",L"Ymir",L"Livai",L"Hansi",L"Erwin",L"Marco",L"Thomas",L"Mihna",L"Mike",L"Auruo",L"Petra",L"Gunther",L"Erd",L"Moblit",L"Ian",L"Rico",L"Mitabi",L"Naile",L"Marlowe",L"Hitch",L"Hannes",L"Pixis",L"Keith",L"Kitz",L"Zackley",L"Nanaba",L"Gelgar",L"Daz",L"Avatar",L"Hooded/Cloaked Avatar"};
    return names;
}
COLORREF blendColor(COLORREF from,COLORREF to,float amount) {
    amount=(std::max)(0.0f,(std::min)(1.0f,amount));
    return RGB(static_cast<BYTE>(GetRValue(from)+(GetRValue(to)-GetRValue(from))*amount),
        static_cast<BYTE>(GetGValue(from)+(GetGValue(to)-GetGValue(from))*amount),
        static_cast<BYTE>(GetBValue(from)+(GetBValue(to)-GetBValue(from))*amount));
}
void drawOption(HDC dc, Option& option, RECT area, POINT cursor, bool enabled=true, float reveal=1.0f) {
    option.row = area;
    const bool hovered = enabled && PtInRect(&area, cursor) != FALSE;
    if (hovered && reveal>=0.98f) {
        RECT hover = area; hover.right -= 1;
        rounded(dc, hover, 10, RGB(37,39,42), RGB(37,39,42));
    }
    if (option.valueField) {
        option.box={area.left+1,area.top+2,area.right-1,area.top+23};
        rounded(dc,option.box,8,RGB(21,22,24),RGB(75,77,81));
        return;
    }
    if (option.dataField) {
        const int inset=option.child?14:0;
        const int valueWidth=option.dataSize==4?110:94;
        option.box={area.right-valueWidth,area.top+1,area.right,area.bottom-1};
        label(dc,area.left+inset,area.top+2,option.name,TEXT,compactOptionFont);
        rounded(dc,option.box,7,RGB(21,22,24),RGB(65,67,71));
        std::wstring value=L"—";
        if (option.valueAvailable) {
            if (option.dataOffset==0x08 && option.numericValue<playerCharacterNames().size())
                value=playerCharacterNames()[static_cast<size_t>(option.numericValue)];
            else value=std::to_wstring(option.numericValue);
        }
        label(dc,option.box.left+8,option.box.top+1,value,option.valueAvailable?TEXT:MUTED,tinyOptionFont);
        return;
    }
    const int size = 21;
    option.box = {area.right - size, area.top + 2, area.right, area.top + size + 2};
    const int inset=option.child?14:0;
    const int maxTextWidth=(area.right-area.left)-31-inset;
    HFONT fittedFont=optionFont;
    for (HFONT candidate : {optionFont,compactOptionFont,tinyOptionFont,microOptionFont}) {
        HGDIOBJ oldFont=SelectObject(dc,candidate);
        SIZE textSize{};
        GetTextExtentPoint32W(dc,option.name.c_str(),static_cast<int>(option.name.size()),&textSize);
        SelectObject(dc,oldFont);
        if (textSize.cx<=maxTextWidth) { fittedFont=candidate; break; }
        fittedFont=candidate;
    }
    const COLORREF optionText=enabled?(hovered?RGB(255,255,255):TEXT):RGB(92,94,98);
    label(dc, area.left+inset, area.top + 2, option.name, blendColor(PANEL,optionText,reveal), fittedFont);
    if (reveal>=0.98f) drawCheckBox(dc,option.box,option.checked,0.0f,enabled);
    else {
        RECT fadingBox=option.box;
        rounded(dc,fadingBox,8,blendColor(PANEL,enabled?(option.checked?RED:RGB(35,36,39)):RGB(27,28,30),reveal),
            blendColor(PANEL,enabled?(option.checked?RED:RGB(75,77,81)):RGB(43,44,47),reveal));
    }
}
void drawPanel(HDC dc, RECT r, const std::wstring& title, std::vector<Option>& options, size_t offset, size_t count, POINT cursor, bool masterEnabled, bool showValueFields=false, float reveal=1.0f, int scrollRows=0) {
    rounded(dc, r, 18, PANEL, BORDER);
    label(dc, r.left+15, r.top+12, title, TEXT, boldFont);
    HPEN p = CreatePen(PS_SOLID, 1, RGB(245,246,248)); auto old=SelectObject(dc,p);
    MoveToEx(dc,r.left+1,r.top+38,nullptr); LineTo(dc,r.right-1,r.top+38); SelectObject(dc,old); DeleteObject(p);
    const int rowH = 27;
    int rendered=0;
    for (size_t i=0; i<count; ++i) {
        Option& option=options[offset+i];
        if (option.requiresPersonalScript && !masterEnabled) { option.row={}; option.box={}; continue; }
        if (option.valueField && !showValueFields) { option.row={}; option.box={}; continue; }
        bool ancestorsOpen=true;
        int parent=option.parentIndex;
        for (size_t depth=0;parent>=0 && depth<options.size();++depth) {
            if (parent>=static_cast<int>(options.size()) || !options[parent].checked) { ancestorsOpen=false; break; }
            parent=options[parent].parentIndex;
        }
        if (!ancestorsOpen) { option.row={}; option.box={}; continue; }
        const int rowIndex=rendered++;
        if (rowIndex<scrollRows) { option.row={}; option.box={}; continue; }
        const bool gated=option.requiresPersonalScript;
        const float optionReveal=gated?reveal:1.0f;
        const int rowY=r.top+42+(rowIndex-scrollRows)*rowH+(gated?static_cast<int>((1.0f-optionReveal)*12.0f):0);
        RECT row{r.left+15,rowY,r.right-15,rowY+rowH-3};
        if (row.bottom > r.bottom - 8) { option.row={}; option.box={}; continue; }
        const bool enabled=!option.requiresPersonalScript || masterEnabled;
        drawOption(dc, option, row, cursor, enabled,optionReveal);
    }
}
void loadNavIcons() {
    wchar_t executablePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr,executablePath,MAX_PATH);
    std::wstring folder(executablePath);
    const size_t slash=folder.find_last_of(L"\\/");
    if (slash != std::wstring::npos) folder.resize(slash+1);
    const wchar_t* filenames[]={L"house.png",L"user-round.png",L"globe.png",L"sliders-horizontal.png",L"settings.png"};
    for (int i=0;i<5;++i) {
        const std::wstring path=folder+L"assets\\icons\\"+filenames[i];
        navIcons[i]=new Gdiplus::Bitmap(path.c_str());
        if (navIcons[i]->GetLastStatus()!=Gdiplus::Ok) { delete navIcons[i]; navIcons[i]=nullptr; }
    }
}
void drawNavIcon(HDC dc, int index, RECT target, bool selected, float opacity=1.0f) {
    if (!navIcons[index]) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::ImageAttributes attributes;
    Gdiplus::ColorMatrix matrix{};
    matrix.m[0][0]=matrix.m[1][1]=matrix.m[2][2]=matrix.m[4][4]=1.0f;
    matrix.m[3][3]=(selected?1.0f:0.58f)*opacity;
    attributes.SetColorMatrix(&matrix,Gdiplus::ColorMatrixFlagsDefault,Gdiplus::ColorAdjustTypeBitmap);
    Gdiplus::Rect destination(target.left,target.top,target.right-target.left,target.bottom-target.top);
    graphics.DrawImage(navIcons[index],destination,0,0,navIcons[index]->GetWidth(),navIcons[index]->GetHeight(),Gdiplus::UnitPixel,&attributes);
}
void drawNavigation(HDC dc, int x, int y, POINT cursor, float squish, bool saveHitBoxes, float reveal=1.0f) {
    const bool scriptEnabled=!categories.empty() && !categories[0].options.empty() && categories[0].options[0].checked;
    const int visibleCategories=scriptEnabled?static_cast<int>(categories.size()):1;
    const int fullNavW=12+56*visibleCategories;
    const int navW=scriptEnabled?static_cast<int>(std::lround((12+56)+(fullNavW-(12+56))*reveal)):(12+56);
    const int navH=58;
    RECT nav{x,y,x+navW,y+navH};
    rounded(dc,nav,19,RGB(17,18,20),RGB(17,18,20));
    if (saveHitBoxes) navRects.clear();
    for (int i=0;i<visibleCategories;++i) {
        const float appCenter=static_cast<float>(x)+static_cast<float>(navW)*0.5f;
        const float finalLeft=appCenter-static_cast<float>(fullNavW)*0.5f;
        const float finalCenter=finalLeft+34.0f+i*56.0f;
        const float centerX=appCenter+(finalCenter-appCenter)*(scriptEnabled?reveal:1.0f);
        const int bx=static_cast<int>(std::lround(centerX-24.0f));
        RECT hit{bx,y+4,bx+48,y+48};
        if (saveHitBoxes) navRects.push_back(hit);
        const bool selected=i==activeCategory;
        const bool hovered=PtInRect(&hit,cursor)!=FALSE;
        const float pulse=(selected && i==squishCategory) ? squish : 0.0f;
        const float sx=1.0f-0.12f*pulse, sy=1.0f-0.24f*pulse;
        const float centerY=static_cast<float>(y+26);
        RECT button{
            static_cast<LONG>(centerX-24.0f*sx), static_cast<LONG>(centerY-22.0f*sy),
            static_cast<LONG>(centerX+24.0f*sx), static_cast<LONG>(centerY+22.0f*sy)};
        const COLORREF fill=hovered?RGB(39,41,44):RGB(17,18,20);
        rounded(dc,button,14,fill,fill);
        RECT icon{
            static_cast<LONG>(centerX-12.0f*sx), static_cast<LONG>(y+29-12.0f*sy),
            static_cast<LONG>(centerX+12.0f*sx), static_cast<LONG>(y+29+12.0f*sy)};
        const float iconOpacity=(scriptEnabled && i>0)?reveal:1.0f;
        drawNavIcon(dc,i,icon,selected,iconOpacity);
        if (selected) {
            Gdiplus::Graphics graphics(dc); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::SolidBrush dot(gdiColor(blendColor(RGB(17,18,20),RGB(245,246,248),reveal)));
            graphics.FillEllipse(&dot,centerX-3.0f,static_cast<float>(y+49),6.0f,6.0f);
        }
    }
}
void destroySquishBuffer() {
    if (squishBufferDC && squishPreviousBitmap)
        SelectObject(squishBufferDC, squishPreviousBitmap);
    if (squishBufferBitmap) { DeleteObject(squishBufferBitmap); squishBufferBitmap=nullptr; }
    if (squishBufferDC) { DeleteDC(squishBufferDC); squishBufferDC=nullptr; }
    squishPreviousBitmap=nullptr;
}
void drawPanelSquish(HWND hwnd, float pulse) {
    if (!panelSquishFrames[0] || !panelSquishFrames[1]) return;
    HDC screen=GetDC(hwnd);
    if (!screen) return;
    RECT area{panelSquishRects[0].left,panelSquishRects[0].top,
        panelSquishRects[1].right,panelSquishRects[0].bottom+4};
    HBRUSH background=CreateSolidBrush(BG);
    FillRect(screen,&area,background); DeleteObject(background);
    const int offsetY=static_cast<int>(std::lround(4.0f*pulse));
    for (int i=0;i<2;++i) {
        const int width=panelSquishRects[i].right-panelSquishRects[i].left;
        const int height=panelSquishRects[i].bottom-panelSquishRects[i].top;
        HDC source=CreateCompatibleDC(screen);
        HBITMAP old=static_cast<HBITMAP>(SelectObject(source,panelSquishFrames[i]));
        BitBlt(screen,panelSquishRects[i].left,panelSquishRects[i].top+offsetY,
            width,height,source,0,0,SRCCOPY);
        SelectObject(source,old); DeleteDC(source);
    }
    ReleaseDC(hwnd,screen);
}
void paint(HWND hwnd) {
    PAINTSTRUCT ps{}; HDC target=BeginPaint(hwnd,&ps); RECT client{}; GetClientRect(hwnd,&client);
    const int width = client.right - client.left, height = client.bottom - client.top;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP backBuffer = CreateCompatibleBitmap(target, width, height);
    HBITMAP previousBitmap = static_cast<HBITMAP>(SelectObject(dc, backBuffer));
    HBRUSH bg=CreateSolidBrush(BG); FillRect(dc,&client,bg); DeleteObject(bg);
    const int w=client.right, h=client.bottom;
    const std::wstring& categoryTitle = categories[activeCategory].name;
    SIZE titleSize{}; GetTextExtentPoint32W(dc, categoryTitle.c_str(), static_cast<int>(categoryTitle.size()), &titleSize);
    label(dc,(w-titleSize.cx)/2,17,categoryTitle,TEXT,boldFont);
    minimizeRect={w-64,15,w-42,37}; closeRect={w-38,15,w-16,37};
    POINT cursor{}; GetCursorPos(&cursor); ScreenToClient(hwnd,&cursor);
    Gdiplus::Graphics captionGraphics(dc); captionGraphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen captionPen(gdiColor(PtInRect(&closeRect,cursor)?RGB(245,246,248):MUTED),1.7f);
    captionPen.SetStartCap(Gdiplus::LineCapRound); captionPen.SetEndCap(Gdiplus::LineCapRound);
    captionGraphics.DrawLine(&captionPen,static_cast<float>(closeRect.left+7),static_cast<float>(closeRect.top+7),static_cast<float>(closeRect.right-7),static_cast<float>(closeRect.bottom-7));
    captionGraphics.DrawLine(&captionPen,static_cast<float>(closeRect.right-7),static_cast<float>(closeRect.top+7),static_cast<float>(closeRect.left+7),static_cast<float>(closeRect.bottom-7));
    Gdiplus::Pen minimizePen(gdiColor(PtInRect(&minimizeRect,cursor)?RGB(245,246,248):MUTED),1.7f);
    minimizePen.SetStartCap(Gdiplus::LineCapRound); minimizePen.SetEndCap(Gdiplus::LineCapRound);
    captionGraphics.DrawLine(&minimizePen,static_cast<float>(minimizeRect.left+6),static_cast<float>(minimizeRect.top+11),static_cast<float>(minimizeRect.right-6),static_cast<float>(minimizeRect.top+11));
    HPEN line=CreatePen(PS_SOLID,1,BORDER); HGDIOBJ old=SelectObject(dc,line); MoveToEx(dc,0,53,nullptr); LineTo(dc,w,53); SelectObject(dc,old); DeleteObject(line);
    const int gap=12;
    const int contentWidth=(std::min)(600,w-40);
    const int panelW=(contentWidth-gap)/2;
    const int panelH=(std::min)(280,(std::max)(220,h-150));
    const int contentX=(w-contentWidth)/2;
    const int top=64;
    RECT left{contentX,top,contentX+panelW,top+panelH};
    RECT right{contentX+panelW+gap,top,contentX+contentWidth,top+panelH};
    currentPanelRects[0]=left; currentPanelRects[1]=right;
    GetCursorPos(&cursor); ScreenToClient(hwnd, &cursor);
    const Category& category = categories[activeCategory];
    const bool personalEnabled=!categories.empty() && !categories[0].options.empty() && categories[0].options[0].checked;
    float revealProgress=1.0f;
    if (scriptRevealAnimating) {
        const float raw=(std::min)(1.0f,static_cast<float>(GetTickCount64()-scriptRevealStart)/260.0f);
        revealProgress=raw*raw*(3.0f-2.0f*raw);
    }
    const size_t visibleCount=category.options.size();
    const size_t leftCount=(visibleCount+1)/2;
    const size_t rightCount=visibleCount-leftCount;
    const bool showValueFields=activeCategory==1 && !category.options.empty() && category.options[0].checked;
    if (activeCategory==0 && !personalEnabled) {
        RECT homePanel{contentX,top,contentX+contentWidth,top+panelH};
        drawPanel(dc,homePanel,category.leftTitle,categories[activeCategory].options,0,1,cursor,false,false);
    } else {
        drawPanel(dc,left,category.leftTitle,categories[activeCategory].options,0,leftCount,cursor,personalEnabled,showValueFields,revealProgress,scrollOffsets[activeCategory][0]);
        drawPanel(dc,right,category.rightTitle,categories[activeCategory].options,leftCount,rightCount,cursor,personalEnabled,showValueFields,revealProgress,scrollOffsets[activeCategory][1]);
    }
    const int visibleCategories=personalEnabled?static_cast<int>(categories.size()):1;
    const int fullNavW=12+56*visibleCategories;
    const int navW=personalEnabled?static_cast<int>(std::lround((12+56)+(fullNavW-(12+56))*revealProgress)):fullNavW;
    const int navY=h-58-30, navX=(w-navW)/2;
    drawNavigation(dc,navX,navY,cursor,0.0f,true,revealProgress);
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, previousBitmap); DeleteObject(backBuffer); DeleteDC(dc);
    EndPaint(hwnd,&ps);
}
int hitTestElement(POINT point) {
    if (PtInRect(&closeRect, point)) return -2;
    if (PtInRect(&minimizeRect, point)) return -3;
    for (int i = 0; i < static_cast<int>(navRects.size()); ++i)
        if (PtInRect(&navRects[i], point)) return i;
    for (int i = 0; i < static_cast<int>(categories[activeCategory].options.size()); ++i)
        if (PtInRect(&categories[activeCategory].options[i].row, point)) return 100 + i;
    return -1;
}
void init() {
    auto addOption=[](Category& category,const wchar_t* name,bool gated=false) {
        Option option; option.name=name; option.requiresPersonalScript=gated;
        category.options.push_back(std::move(option));
    };
    auto addDataGroup=[](Category& category,const wchar_t* name,int parent) {
        Option option; option.name=name; option.uiOnly=true; option.child=true; option.parentIndex=parent;
        category.options.push_back(std::move(option));
    };
    auto addDataField=[](Category& category,const wchar_t* name,uintptr_t offset,size_t size,int parent) {
        Option option; option.name=name; option.dataField=true; option.child=true;
        option.parentIndex=parent; option.dataOffset=offset; option.dataSize=size;
        category.options.push_back(std::move(option));
    };
    Category main; main.name=L"Main"; main.leftTitle=L"Main"; main.rightTitle=L"Resources";
    addOption(main,L"Personal Script");
    addOption(main,L"Max Money",true); addOption(main,L"Max City Materials",true);
    addOption(main,L"Max Wing of Freedom Badges",true);
    addOption(main,L"All Materials",true); addOption(main,L"All Gifts",true);
    categories.push_back(std::move(main));

    Category player; player.name=L"Player Data"; player.leftTitle=L"Player Data"; player.rightTitle=L"Change Values";
    addOption(player,L"Player Data");
    addDataGroup(player,L"Current Character",0);
    addDataField(player,L"Name",0x08,1,1);
    addDataField(player,L"Level / Rank (-1)",0x9C,1,1);
    addDataField(player,L"Experience",0x98,4,1);
    addDataGroup(player,L"Stats",0);
    addDataField(player,L"Strength",0x88,1,5);
    addDataField(player,L"Dexterity",0x8A,1,5);
    addDataField(player,L"Concentration",0x8C,1,5);
    addDataField(player,L"Agility",0x8E,1,5);
    addDataField(player,L"Health",0x90,1,5);
    addDataField(player,L"Leadership",0x92,1,5);
    addDataGroup(player,L"Skills",0);
    addOption(player,L"Equipe All Skills");
    player.options.back().child=true; player.options.back().parentIndex=12;
    addDataGroup(player,L"Equipment",0);
    addOption(player,L"Blades Reinforcement");
    addOption(player,L"Scabbards / Canisters Reinforcement");
    addOption(player,L"Omni-Directional Mobility Gears Reinforcement");
    addOption(player,L"Gun Barrel Reinforcement");
    addOption(player,L"Ammo Belt / Canister Reinforcement");
    addOption(player,L"Anti-Personnel ODM Reinforcement");
    for (size_t i=15;i<=20;++i) { player.options[i].child=true; player.options[i].parentIndex=14; }
    addOption(player,L"Titan Timer"); addOption(player,L"Partners Skills No Cooldown");
    addOption(player,L"Max Reinforce New Equipment (Buy)");
    addOption(player,L"Max Awakening Ability Points (+140)");
    addOption(player,L"Showdown Equipment Always Available");
    addOption(player,L"Infinite Showdown Equipment (on rescue missions)");
    addOption(player,L"Battle Alert Signal Infinite Duration");
    addOption(player,L"Spawn Dire Elimination"); addOption(player,L"Auto End/Win Battle");
    categories.push_back(std::move(player));

    Category world; world.name=L"World"; world.leftTitle=L"World"; world.rightTitle=L"Supplies";
    addOption(world,L"Infinite Gas",true); addOption(world,L"Infinite Blades",true);
    addOption(world,L"Infinite Ammo",true); addOption(world,L"Infinite Combat Items",true);
    categories.push_back(std::move(world));

    Category tools; tools.name=L"Tools"; tools.leftTitle=L"Combat Tools"; tools.rightTitle=L"Automation";
    addOption(tools,L"Infinite Dash & Multi-Attacks",true); addOption(tools,L"Auto Hook Drive",true);
    addOption(tools,L"Auto Overall Score SS",true);
    categories.push_back(std::move(tools));

    Category social; social.name=L"Social"; social.leftTitle=L"Social"; social.rightTitle=L"Progress";
    addOption(social,L"Quick Friendship Gain",true); addOption(social,L"Quick Camaraderie Points",true);
    categories.push_back(std::move(social));
}
void closeValueEditor() {
    HWND editor=activeValueEditor;
    activeValueEditor=nullptr; activeValueOption=-1; activeValueIsCharacter=false;
    if (editor && IsWindow(editor)) DestroyWindow(editor);
}
void commitValueEditor(HWND hwnd,uint64_t value) {
    if (!activeValueEditor || activeValueOption<0 || activeCategory!=1) return;
    Option& option=categories[1].options[activeValueOption];
    const uint64_t maximum=option.dataSize==1?0xFFull:0xFFFFFFFFull;
    if (value>maximum) {
        MessageBoxW(hwnd,L"Value is outside the range for this field.",L"Invalid value",MB_OK|MB_ICONINFORMATION);
        closeValueEditor(); InvalidateRect(hwnd,nullptr,FALSE);
        return;
    }
    std::vector<uint8_t> bytes(option.dataSize);
    for (size_t i=0;i<option.dataSize;++i) bytes[i]=static_cast<uint8_t>(value>>(i*8));
    std::wstring error;
    if (!featureController.writePlayerField(option.dataOffset,bytes,error)) {
        MessageBoxW(hwnd,error.c_str(),L"Could not update Player Data",MB_OK|MB_ICONWARNING);
        closeValueEditor(); InvalidateRect(hwnd,nullptr,FALSE);
        return;
    }
    option.numericValue=value; option.valueAvailable=true;
    closeValueEditor();
    InvalidateRect(hwnd,nullptr,FALSE);
}
void beginValueEdit(HWND hwnd,int optionIndex) {
    if (activeValueEditor) closeValueEditor();
    if (optionIndex<0 || optionIndex>=static_cast<int>(categories[1].options.size())) return;
    Option& option=categories[1].options[optionIndex];
    if (!option.valueAvailable) {
        MessageBoxW(hwnd,L"The game has not exposed this Player Data value yet. Open the character/status screen, then try again.",L"Value not available",MB_OK|MB_ICONINFORMATION);
        return;
    }
    RECT r=option.box;
    activeValueOption=optionIndex;
    if (option.dataOffset==0x08) {
        activeValueIsCharacter=true;
        activeValueEditor=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,
            r.left,r.top,r.right-r.left,180,hwnd,reinterpret_cast<HMENU>(2001),GetModuleHandleW(nullptr),nullptr);
        if (!activeValueEditor) { activeValueOption=-1; activeValueIsCharacter=false; return; }
        for (const auto& name:playerCharacterNames()) SendMessageW(activeValueEditor,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
        SendMessageW(activeValueEditor,CB_SETDROPPEDWIDTH,220,0);
        SendMessageW(activeValueEditor,CB_SETCURSEL,static_cast<WPARAM>(option.numericValue),0);
        SetFocus(activeValueEditor);
    } else {
        activeValueIsCharacter=false;
        activeValueEditor=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|ES_NUMBER,
            r.left,r.top,r.right-r.left,r.bottom-r.top,hwnd,reinterpret_cast<HMENU>(2001),GetModuleHandleW(nullptr),nullptr);
        if (!activeValueEditor) { activeValueOption=-1; return; }
        SetWindowTextW(activeValueEditor,std::to_wstring(option.numericValue).c_str());
        SendMessageW(activeValueEditor,EM_SETSEL,0,-1);
        SetFocus(activeValueEditor);
    }
}
bool refreshPlayerValues() {
    if (categories.size()<2 || categories[1].options.empty() || !categories[1].options[0].checked ||
        processManager.snapshot().state!=GameConnectionState::Connected) return false;
    bool changed=false;
    for (auto& option:categories[1].options) {
        if (!option.dataField) continue;
        std::vector<uint8_t> bytes; std::wstring ignored;
        const bool available=featureController.readPlayerField(option.dataOffset,option.dataSize,bytes,ignored);
        uint64_t value=0;
        if (available) for (size_t i=0;i<bytes.size();++i) value|=static_cast<uint64_t>(bytes[i])<<(i*8);
        if (option.valueAvailable!=available || (available && option.numericValue!=value)) changed=true;
        option.valueAvailable=available;
        if (available) option.numericValue=value;
    }
    return changed;
}
void applyRoundedWindow(HWND hwnd) {
    const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
        &preference, sizeof(preference)))) return;
    RECT rect{}; GetWindowRect(hwnd,&rect);
    HRGN region=CreateRoundRectRgn(0,0,rect.right-rect.left+1,rect.bottom-rect.top+1,28,28);
    if (!SetWindowRgn(hwnd,region,TRUE)) DeleteObject(region);
}
LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
    case WM_COMMAND:
        if (LOWORD(wp)==2001 && activeValueEditor) {
            const UINT notification=HIWORD(wp);
            if (activeValueIsCharacter && notification==CBN_SELCHANGE) {
                const LRESULT selected=SendMessageW(activeValueEditor,CB_GETCURSEL,0,0);
                if (selected>=0) commitValueEditor(hwnd,static_cast<uint64_t>(selected));
                return 0;
            }
            if (!activeValueIsCharacter && notification==EN_KILLFOCUS) {
                wchar_t text[64]{}; GetWindowTextW(activeValueEditor,text,64);
                wchar_t* end=nullptr; const unsigned long long value=std::wcstoull(text,&end,10);
                if (end==text || (end && *end!=L'\0')) {
                    MessageBoxW(hwnd,L"Enter a whole number for this field.",L"Invalid value",MB_OK|MB_ICONINFORMATION);
                    closeValueEditor();
                } else commitValueEditor(hwnd,static_cast<uint64_t>(value));
                return 0;
            }
        }
        break;
    case WM_PAINT: paint(hwnd); return 0;
    case WM_MOUSEMOVE: {
        if (!trackingMouseLeave) {
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            trackingMouseLeave = true;
        }
        const POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const int nextHovered = hitTestElement(point);
        // Redrawing the entire UI for every mouse-move message starved the tab
        // animation. Only repaint when the pointer crosses a hoverable control.
        if (nextHovered != hoveredElement) {
            hoveredElement = nextHovered;
            if (!squishAnimating) InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouseLeave = false;
        if (hoveredElement != -1) { hoveredElement = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; ScreenToClient(hwnd,&point);
        for (int side=0;side<2;++side) if (PtInRect(&currentPanelRects[side],point)) {
            const int delta=GET_WHEEL_DELTA_WPARAM(wp);
            scrollOffsets[activeCategory][side]=(std::max)(0,scrollOffsets[activeCategory][side]+(delta<0?3:-3));
            InvalidateRect(hwnd,&currentPanelRects[side],FALSE);
            return 0;
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if (activeValueEditor) SetFocus(hwnd);
        if (PtInRect(&closeRect,p)) { DestroyWindow(hwnd); return 0; }
        if (PtInRect(&minimizeRect,p)) { ShowWindow(hwnd,SW_MINIMIZE); return 0; }
        for(int i=0;i<(int)navRects.size();++i) if(PtInRect(&navRects[i],p)) {
            if (activeCategory!=i) {
                RECT client{}; GetClientRect(hwnd,&client);
                if (squishAnimating) {
                    KillTimer(hwnd,1); squishAnimating=false; destroySquishBuffer(); clearPanelSquish();
                    if (timerPrecisionActive) { timeEndPeriod(1); timerPrecisionActive=false; }
                }
                activeCategory=i;
                scrollOffsets[i][0]=scrollOffsets[i][1]=0;
                squishCategory=i;
                squishOptionMode=false;
                squishStartTime=GetTickCount64();
                squishAnimating=true;
                if (!timerPrecisionActive) { timeBeginPeriod(1); timerPrecisionActive=true; }
                HDC screen=GetDC(hwnd);
                const int navWidth=12+56*static_cast<int>(categories.size());
                if (screen && !squishBufferDC) {
                    squishBufferDC=CreateCompatibleDC(screen);
                    squishBufferBitmap=CreateCompatibleBitmap(screen,navWidth,58);
                    squishPreviousBitmap=static_cast<HBITMAP>(SelectObject(squishBufferDC,squishBufferBitmap));
                }
                if (screen) ReleaseDC(hwnd,screen);
                SetTimer(hwnd,1,16,nullptr);
                InvalidateRect(hwnd,nullptr,FALSE);
                UpdateWindow(hwnd);
                capturePanelFrames(hwnd);
                return 0;
            }
            InvalidateRect(hwnd,nullptr,FALSE); return 0;
        }
        for (int index=0; index<static_cast<int>(categories[activeCategory].options.size()); ++index) {
            Option& option=categories[activeCategory].options[index];
            if (option.dataField && PtInRect(&option.box,p)) {
                beginValueEdit(hwnd,index);
                return 0;
            }
            if (PtInRect(&option.box,p)) {
                if (option.valueField) return 0;
                if (!(activeCategory==0 && index==0) && !option.uiOnly && processManager.snapshot().state!=GameConnectionState::Connected) return 0;
                if (option.requiresPersonalScript && !categories[0].options[0].checked) return 0;
                if (squishAnimating) {
                    KillTimer(hwnd,1); squishAnimating=false; destroySquishBuffer(); clearPanelSquish();
                    if (timerPrecisionActive) { timeEndPeriod(1); timerPrecisionActive=false; }
                }
                option.checked=!option.checked;
                const bool isPersonalScript=activeCategory==0 && index==0;
                if (!isPersonalScript && !option.uiOnly) {
                    std::wstring featureError;
                    if (!featureController.setEnabled(option.name,option.checked,featureError)) {
                        option.checked=!option.checked;
                        MessageBoxW(hwnd,featureError.c_str(),L"Feature unavailable",MB_OK|MB_ICONINFORMATION);
                        InvalidateRect(hwnd,nullptr,FALSE);
                        return 0;
                    }
                }
                if (activeCategory==0 && index==0) {
                    scriptRevealAnimating=option.checked;
                    scriptRevealStart=GetTickCount64();
                }
                if (activeCategory==0 && index==0 && !option.checked) {
                    featureController.disableAll(processManager.snapshot().state==GameConnectionState::Connected);
                    for (size_t ci=0;ci<categories.size();++ci) {
                        for (size_t oi=0;oi<categories[ci].options.size();++oi) {
                            if (ci==0 && oi==0) continue;
                            categories[ci].options[oi].checked=false;
                        }
                    }
                }
                if (activeCategory==1 && index==0) {
                    const int groups[]={1,5,12,14};
                    for (int group:groups) categories[1].options[group].checked=option.checked;
                    if (!option.checked) {
                        for (size_t oi=1;oi<categories[1].options.size();++oi) {
                            categories[1].options[oi].checked=false;
                            if (categories[1].options[oi].dataField) categories[1].options[oi].valueAvailable=false;
                        }
                    }
                }
                squishAnimating=true; squishOptionMode=true;
                squishOptionCategory=activeCategory; squishOptionIndex=index;
                squishStartTime=GetTickCount64();
                if (!timerPrecisionActive) { timeBeginPeriod(1); timerPrecisionActive=true; }
                InvalidateRect(hwnd,nullptr,FALSE); UpdateWindow(hwnd);
                SetTimer(hwnd,1,16,nullptr);
                return 0;
            }
        }
        return 0;
    }
    case WM_NCHITTEST: {
        const POINT screenPoint{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        POINT clientPoint=screenPoint; ScreenToClient(hwnd,&clientPoint);
        if (PtInRect(&closeRect,clientPoint) || PtInRect(&minimizeRect,clientPoint)) return HTCLIENT;
        if (clientPoint.y >= 0 && clientPoint.y < 54) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_KEYDOWN:
        if (wp==VK_F5) { processManager.requestRetry(); return 0; }
        break;
    case WM_TIMER:
        if (wp==2) {
            const GameConnectionSnapshot current=processManager.snapshot();
            const bool valuesChanged=refreshPlayerValues();
            if (lastUiConnection.state==GameConnectionState::Connected && current.state!=GameConnectionState::Connected) {
                featureController.disableAll(false);
                if (activeValueEditor) closeValueEditor();
                for (size_t ci=0;ci<categories.size();++ci) {
                    for (size_t oi=0;oi<categories[ci].options.size();++oi) {
                        if (ci==0 && oi==0) continue;
                        categories[ci].options[oi].checked=false;
                    }
                }
                for (auto& option:categories[1].options) if (option.dataField) option.valueAvailable=false;
                InvalidateRect(hwnd,nullptr,FALSE);
            } else if (valuesChanged) InvalidateRect(hwnd,nullptr,FALSE);
            lastUiConnection=current;
            return 0;
        }
        if (wp == 1 && squishAnimating) {
            const float duration=scriptRevealAnimating?260.0f:190.0f;
            const float progress=(std::min)(1.0f,static_cast<float>(GetTickCount64()-squishStartTime)/duration);
            const float pulse=static_cast<float>(sin(3.14159265358979323846*progress));
            RECT client{}; GetClientRect(hwnd,&client);
            const int navWidth=12+56*static_cast<int>(categories.size());
            const int navX=(static_cast<int>(client.right)-navWidth)/2;
            const int navY=static_cast<int>(client.bottom)-58-30;
            POINT cursor{}; GetCursorPos(&cursor); ScreenToClient(hwnd,&cursor);
            if (squishOptionMode && !scriptRevealAnimating) {
                if (squishOptionCategory==activeCategory && squishOptionIndex>=0 &&
                    squishOptionIndex<static_cast<int>(categories[activeCategory].options.size())) {
                    Option& option=categories[activeCategory].options[squishOptionIndex];
                    HDC screen=GetDC(hwnd);
                    if (screen) {
                        HBRUSH rowBrush=CreateSolidBrush(hoveredElement==100+squishOptionIndex?RGB(37,39,42):PANEL);
                        FillRect(screen,&option.box,rowBrush); DeleteObject(rowBrush);
                        drawCheckBox(screen,option.box,option.checked,pulse);
                        ReleaseDC(hwnd,screen);
                    }
                }
            } else if (squishBufferDC && squishBufferBitmap) {
                drawNavigation(squishBufferDC,0,0,POINT{cursor.x-navX,cursor.y-navY},pulse,false);
                HDC screen=GetDC(hwnd);
                if (screen) {
                        BitBlt(screen,navX,navY,navWidth,58,squishBufferDC,0,0,SRCCOPY);
                    ReleaseDC(hwnd,screen);
                }
            }
            if (!squishOptionMode) drawPanelSquish(hwnd,pulse);
            if (scriptRevealAnimating) InvalidateRect(hwnd,nullptr,FALSE);
            if (progress>=1.0f) {
                squishAnimating=false;
                scriptRevealAnimating=false;
                KillTimer(hwnd,1);
                destroySquishBuffer();
                clearPanelSquish();
                if (timerPrecisionActive) { timeEndPeriod(1); timerPrecisionActive=false; }
                if (squishOptionMode) InvalidateRect(hwnd,nullptr,FALSE);
                else { RECT nav{navX,navY,navX+navWidth,navY+58}; InvalidateRect(hwnd,&nav,FALSE); }
            }
            return 0;
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY:
        KillTimer(hwnd,2);
        if (activeValueEditor) closeValueEditor();
        featureController.disableAll(processManager.snapshot().state==GameConnectionState::Connected);
        processManager.stop();
        if (squishAnimating) KillTimer(hwnd,1);
        squishAnimating=false;
        destroySquishBuffer();
        clearPanelSquish();
        if (timerPrecisionActive) { timeEndPeriod(1); timerPrecisionActive=false; }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken,&gdiplusInput,nullptr) != Gdiplus::Ok) return 1;
    init();
    loadNavIcons();
    font=CreateFontW(18,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    optionFont=CreateFontW(17,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    compactOptionFont=CreateFontW(14,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    tinyOptionFont=CreateFontW(11,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    microOptionFont=CreateFontW(9,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    boldFont=CreateFontW(19,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    WNDCLASSW wc{}; wc.lpfnWndProc=wndProc; wc.hInstance=instance; wc.lpszClassName=L"BlankLauncherWindow"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=CreateSolidBrush(BG); RegisterClassW(&wc);
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"Launcher",WS_POPUP|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,640,440,nullptr,nullptr,instance,nullptr);
    if(!hwnd) { Gdiplus::GdiplusShutdown(gdiplusToken); return 1; }
    applyRoundedWindow(hwnd);
    processManager.start();
    SetTimer(hwnd,2,350,nullptr);
    ShowWindow(hwnd,show);
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    DeleteObject(font); DeleteObject(optionFont); DeleteObject(compactOptionFont); DeleteObject(tinyOptionFont); DeleteObject(microOptionFont); DeleteObject(boldFont);
    for (auto*& icon : navIcons) { delete icon; icon=nullptr; }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}
