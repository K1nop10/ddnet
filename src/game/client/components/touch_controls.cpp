#include "touch_controls.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/external/json-parser/json.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/localization.h>

#include <game/client/components/camera.h>
#include <game/client/components/chat.h>
#include <game/client/components/console.h>
#include <game/client/components/controls.h>
#include <game/client/components/emoticon.h>
#include <game/client/components/menus.h>
#include <game/client/components/spectator.h>
#include <game/client/components/voting.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

using namespace std::chrono_literals;

// TODO: Add user interface to adjust button layout
// TODO: Add "color" property for touch buttons?
// TODO: Add combined weapon picker button that shows all currently available weapons
// TODO: Add "joystick-aim-relative", a virtual joystick that moves the mouse pointer relatively. And add "aim-relative" ingame direct touch input.
// TODO: Add "choice" predefined behavior which shows a selection popup for 2 or more other behaviors?
// TODO: Support changing labels of menu buttons (or support overriding label for all predefined button behaviors)?

static constexpr const char *const ACTION_NAMES[] = {Localizable("Aim"), Localizable("Fire"), Localizable("Hook")};
static constexpr const char *const ACTION_SWAP_NAMES[] = {/* unused */ "", Localizable("Active: Fire"), Localizable("Active: Hook")};
static constexpr const char *const ACTION_COMMANDS[] = {/* unused */ "", "+fire", "+hook"};

static constexpr std::chrono::milliseconds LONG_TOUCH_DURATION = 500ms;
static constexpr std::chrono::milliseconds BIND_REPEAT_INITIAL_DELAY = 250ms;
static constexpr std::chrono::nanoseconds BIND_REPEAT_RATE = std::chrono::nanoseconds(1s) / 15;

static constexpr const char *const CONFIGURATION_FILENAME = "touch_controls.json";
static constexpr int BUTTON_SIZE_SCALE = 1000000;
static constexpr int BUTTON_SIZE_MINIMUM = 0;
static constexpr int BUTTON_SIZE_MAXIMUM = 1000000;

/* This is required for the localization script to find the labels of the default bind buttons specified in the configuration file:
Localizable("Move left") Localizable("Move right") Localizable("Jump") Localizable("Prev. weapon") Localizable("Next weapon")
Localizable("Zoom out") Localizable("Default zoom") Localizable("Zoom in") Localizable("Scoreboard") Localizable("Chat") Localizable("Team chat")
Localizable("Vote yes") Localizable("Vote no") Localizable("Toggle dummy")
*/
bool IsOnLine = [](vec2 target, vec2 a, vec2 b){
	if(minimum(a.x, b.x) > target.x || maximum(a.x, b.x) < target.x || minimum(a.y, b.y) > target.y || maximum(a.y, b.y) < target.y)
		return false;
	float k = (b.y - a.y) / (b.x - a.x);
	float c = (a.y * b.x - a.x * b.y) / (b.x - a.x);
	if(std::abs(k * target.x + c - target.y) < 0.0001f)
		return true;
	return false;
};

bool IsTwoLine = [](vec2 a, vec2 b, vec2 c, vec2 d){
	
};

vec2 TwoLine = [](vec2 a, vec2 b, vec2 c, vec2 d){
	
};

bool LinePointUp = [](vec2 a, vec2 b, vec2 c){
	if(minimum(a.x, b.x) <= c.x && maximum(a.x, b.x) >= c.x && minimum(a.y, b.y) < c.y)
	{
		float k = (b.y - a.y) / (b.x - a.x);
		float d = (a.y * b.x - a.x * b.y) / (b.x - a.x);
		if(c.y - k * c.x - d <= 0.0001f)
			return true;
	}
	return false;
};


CTouchControls::CTouchButton::CTouchButton(CTouchControls *pTouchControls) :
	m_pTouchControls(pTouchControls),
	m_VisibilityCached(false)
{
}

CTouchControls::CTouchButton::CTouchButton(CTouchButton &&Other) noexcept :
	m_pTouchControls(Other.m_pTouchControls),
	m_UnitRect(Other.m_UnitRect),
	m_Shape(Other.m_Shape),
	m_vVisibilities(Other.m_vVisibilities),
	m_pBehavior(std::move(Other.m_pBehavior)),
	m_VisibilityCached(false)
{
	Other.m_pTouchControls = nullptr;
}

CTouchControls::CTouchButton &CTouchControls::CTouchButton::operator=(CTouchButton &&Other) noexcept
{
	m_pTouchControls = Other.m_pTouchControls;
	Other.m_pTouchControls = nullptr;
	m_UnitRect = Other.m_UnitRect;
	m_Shape = Other.m_Shape;
	m_vVisibilities = Other.m_vVisibilities;
	m_pBehavior = std::move(Other.m_pBehavior);
	m_VisibilityCached = false;
	return *this;
}

void CTouchControls::CTouchButton::UpdatePointers()
{
	m_pBehavior->Init(this);
}

void CTouchControls::CTouchButton::UpdateScreenFromUnitRect()
{
	const vec2 ScreenSize = m_pTouchControls->CalculateScreenSize();
	m_ScreenRect.x = m_UnitRect.m_X * ScreenSize.x / BUTTON_SIZE_SCALE;
	m_ScreenRect.y = m_UnitRect.m_Y * ScreenSize.y / BUTTON_SIZE_SCALE;
	m_ScreenRect.w = m_UnitRect.m_W * ScreenSize.x / BUTTON_SIZE_SCALE;
	m_ScreenRect.h = m_UnitRect.m_H * ScreenSize.y / BUTTON_SIZE_SCALE;

	// Enforce circle shape so the screen rect can be used for mapping the touch input position
	if(m_Shape == EButtonShape::CIRCLE)
	{
		if(m_ScreenRect.h > m_ScreenRect.w)
		{
			m_ScreenRect.y += (m_ScreenRect.h - m_ScreenRect.w) / 2.0f;
			m_ScreenRect.h = m_ScreenRect.w;
		}
		else if(m_ScreenRect.w > m_ScreenRect.h)
		{
			m_ScreenRect.x += (m_ScreenRect.w - m_ScreenRect.h) / 2.0f;
			m_ScreenRect.w = m_ScreenRect.h;
		}
	}
}

void CTouchControls::CTouchButton::UpdateBackgroundCorners()
{
	if(m_Shape != EButtonShape::RECT)
	{
		m_BackgroundCorners = IGraphics::CORNER_NONE;
		return;
	}

	// Determine rounded corners based on button layout
	m_BackgroundCorners = IGraphics::CORNER_ALL;

	if(m_UnitRect.m_X == 0)
	{
		m_BackgroundCorners &= ~IGraphics::CORNER_L;
	}
	if(m_UnitRect.m_X + m_UnitRect.m_W == BUTTON_SIZE_SCALE)
	{
		m_BackgroundCorners &= ~IGraphics::CORNER_R;
	}
	if(m_UnitRect.m_Y == 0)
	{
		m_BackgroundCorners &= ~IGraphics::CORNER_T;
	}
	if(m_UnitRect.m_Y + m_UnitRect.m_H == BUTTON_SIZE_SCALE)
	{
		m_BackgroundCorners &= ~IGraphics::CORNER_B;
	}

	const auto &&PointInOrOnRect = [](ivec2 Point, CUnitRect Rect) {
		return Point.x >= Rect.m_X && Point.x <= Rect.m_X + Rect.m_W && Point.y >= Rect.m_Y && Point.y <= Rect.m_Y + Rect.m_H;
	};
	for(const CTouchButton &OtherButton : m_pTouchControls->m_vTouchButtons)
	{
		if(&OtherButton == this || OtherButton.m_Shape != EButtonShape::RECT)
			continue;
		// TODO: This does not consider that button visibilities can change independently, also update corners when any visibility changed
		const bool ExcludingVisibilities = std::any_of(OtherButton.m_vVisibilities.begin(), OtherButton.m_vVisibilities.end(), [&](const CButtonVisibility &OtherVisibility) {
			return std::any_of(m_vVisibilities.begin(), m_vVisibilities.end(), [&](const CButtonVisibility &OurVisibility) {
				return OtherVisibility.m_Type == OurVisibility.m_Type && OtherVisibility.m_Parity != OurVisibility.m_Parity;
			});
		});
		if(ExcludingVisibilities)
			continue;

		if((m_BackgroundCorners & IGraphics::CORNER_TL) && PointInOrOnRect(ivec2(m_UnitRect.m_X, m_UnitRect.m_Y), OtherButton.m_UnitRect))
		{
			m_BackgroundCorners &= ~IGraphics::CORNER_TL;
		}
		if((m_BackgroundCorners & IGraphics::CORNER_TR) && PointInOrOnRect(ivec2(m_UnitRect.m_X + m_UnitRect.m_W, m_UnitRect.m_Y), OtherButton.m_UnitRect))
		{
			m_BackgroundCorners &= ~IGraphics::CORNER_TR;
		}
		if((m_BackgroundCorners & IGraphics::CORNER_BL) && PointInOrOnRect(ivec2(m_UnitRect.m_X, m_UnitRect.m_Y + m_UnitRect.m_H), OtherButton.m_UnitRect))
		{
			m_BackgroundCorners &= ~IGraphics::CORNER_BL;
		}
		if((m_BackgroundCorners & IGraphics::CORNER_BR) && PointInOrOnRect(ivec2(m_UnitRect.m_X + m_UnitRect.m_W, m_UnitRect.m_Y + m_UnitRect.m_H), OtherButton.m_UnitRect))
		{
			m_BackgroundCorners &= ~IGraphics::CORNER_BR;
		}
		if(m_BackgroundCorners == IGraphics::CORNER_NONE)
		{
			break;
		}
	}
}

vec2 CTouchControls::CTouchButton::ClampTouchPosition(vec2 TouchPosition) const
{
	switch(m_Shape)
	{
	case EButtonShape::RECT:
	{
		TouchPosition.x = clamp(TouchPosition.x, m_ScreenRect.x, m_ScreenRect.x + m_ScreenRect.w);
		TouchPosition.y = clamp(TouchPosition.y, m_ScreenRect.y, m_ScreenRect.y + m_ScreenRect.h);
		break;
	}
	case EButtonShape::CIRCLE:
	{
		const vec2 Center = m_ScreenRect.Center();
		const float MaxLength = minimum(m_ScreenRect.w, m_ScreenRect.h) / 2.0f;
		const vec2 TouchDirection = TouchPosition - Center;
		const float Length = length(TouchDirection);
		if(Length > MaxLength)
		{
			TouchPosition = normalize_pre_length(TouchDirection, Length) * MaxLength + Center;
		}
		break;
	}
	default:
		dbg_assert(false, "Unhandled shape");
		break;
	}
	return TouchPosition;
}

bool CTouchControls::CTouchButton::IsInside(vec2 TouchPosition) const
{
	switch(m_Shape)
	{
	case EButtonShape::RECT:
		return m_ScreenRect.Inside(TouchPosition);
	case EButtonShape::CIRCLE:
		return distance(TouchPosition, m_ScreenRect.Center()) <= minimum(m_ScreenRect.w, m_ScreenRect.h) / 2.0f;
	default:
		dbg_assert(false, "Unhandled shape");
		return false;
	}
}

void CTouchControls::CTouchButton::UpdateVisibility()
{
	const bool PrevVisibility = m_VisibilityCached;
	m_VisibilityCached = m_pTouchControls->m_EditingActive || std::all_of(m_vVisibilities.begin(), m_vVisibilities.end(), [&](CButtonVisibility Visibility) {
		return m_pTouchControls->m_aVisibilityFunctions[(int)Visibility.m_Type].m_Function() == Visibility.m_Parity;
	});
	if(m_VisibilityCached && !PrevVisibility)
	{
		m_VisibilityStartTime = time_get_nanoseconds();
	}
}

bool CTouchControls::CTouchButton::IsVisible() const
{
	return m_VisibilityCached;
}

// TODO: Optimization: Use text and quad containers for rendering
void CTouchControls::CTouchButton::Render()
{
	float ctrx = m_UnitRect.m_X + (float)m_UnitRect.m_W / 2.0f;
	float ctry = m_UnitRect.m_Y + (float)m_UnitRect.m_H / 2.0f;
	float alpha = m_pBehavior->IsActive()?g_Config.m_ClButtonAlphaActive / 255.0f:g_Config.m_ClButtonAlpha / 255.0f;
	ColorRGBA ButtonColor;
	auto rainbow = [&](){
		if(!m_pTouchControls->fknanos)
		{
			m_pTouchControls->m_RainbowTimers = time_get_nanoseconds();
			m_pTouchControls->fknanos = 1;
			m_pTouchControls->m_Rainbows = 0.0f;
		}
		if(time_get_nanoseconds() - m_pTouchControls->m_RainbowTimers >= static_cast<std::chrono::milliseconds>((int)(g_Config.m_ClButtonRainbowSpeed / 10.0f)))
		{
			m_pTouchControls->m_RainbowTimers = time_get_nanoseconds();
			m_pTouchControls->m_Rainbows += 1.0f;
		}
		float rainbownums = m_pTouchControls->m_Rainbows + (ctrx + ctry) / 2000000 * 600;
		m_pTouchControls->m_Rainbows = (m_pTouchControls->m_Rainbows >= 600.0f) ? m_pTouchControls->m_Rainbows - 600.0f:m_pTouchControls->m_Rainbows;
		rainbownums = (rainbownums >= 600.0f) ? rainbownums - 600.0f : rainbownums;
		return color_cast<ColorRGBA>(ColorHSLA(rainbownums / 600.0f,g_Config.m_ClButtonRainbowSat / 255.0f,g_Config.m_ClButtonRainbowLig / 255.0f, alpha));
	};
	switch(g_Config.m_ClButtonColorType)
	{
		case 0: ButtonColor = m_pBehavior->IsActive() ? ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);break;
		case 1: ButtonColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClButtonColorStatic));ButtonColor.a = alpha;break;
		case 2: ButtonColor = rainbow();break;
	}
	alpha = m_pBehavior->IsActive()?g_Config.m_ClButtonAlphaActive / 255.0f:g_Config.m_ClButtonAlpha / 255.0f;
	alpha = (g_Config.m_ClButtonColorType == 0) ? 0.25f:alpha;
	switch(m_Shape)
	{
	case EButtonShape::RECT:
	{
		m_ScreenRect.Draw(ColorRGBA(ButtonColor.r,ButtonColor.g,ButtonColor.b,alpha), m_BackgroundCorners, 10.0f);
		break;
	}
	case EButtonShape::CIRCLE:
	{
		const vec2 Center = m_ScreenRect.Center();
		const float Radius = minimum(m_ScreenRect.w, m_ScreenRect.h) / 2.0f;
		m_pTouchControls->Graphics()->TextureClear();
		m_pTouchControls->Graphics()->QuadsBegin();
		m_pTouchControls->Graphics()->SetColor(ColorRGBA(ButtonColor.r,ButtonColor.g,ButtonColor.b,alpha));
		m_pTouchControls->Graphics()->DrawCircle(Center.x, Center.y, Radius, maximum(round_truncate(Radius / 4.0f) & ~1, 32));
		m_pTouchControls->Graphics()->QuadsEnd();
		break;
	}
	default:
		dbg_assert(false, "Unhandled shape");
		break;
	}

	const float FontSize = 22.0f;
	CButtonLabel LabelData = m_pBehavior->GetLabel();
	CUIRect LabelRect;
	m_ScreenRect.Margin(10.0f, &LabelRect);
	SLabelProperties LabelProps;
	LabelProps.m_MaxWidth = LabelRect.w;
	alpha = m_pBehavior->IsActive()?g_Config.m_ClLabelAlphaActive / 255.0f:g_Config.m_ClLabelAlpha / 255.0f;
//ForgottenCat Gonna Do Sonething Unique!
	int j=0;
	char e[1000]="";
	LabelProps.m_vColorSplits.clear();
	if((LabelData.m_Type == CButtonLabel::EType::PLAIN || LabelData.m_Type == CButtonLabel::EType::LOCALIZED) && *LabelData.m_pLabel=='%' && g_Config.m_ClLabelColorType == 0)
	{
		int tmp_length = str_length(LabelData.m_pLabel);
		int a[1000]={};char d[1000]="";
		const char *b = LabelData.m_pLabel;
		for(int i=0;i<tmp_length;i++)
		{
			if(*(b+i) == '%' && i < tmp_length-1)
			{
				a[j] = i - 2*j;
				d[j] = *(b+i+1);
				j++;i++;
			}
		}
		if(j!=0)
		{
			for(int i=0;i<j;i++)
			{
				int tmpf=0;
				if(a[i+1])
					tmpf=1;
				switch(d[i])
				{
					case 'c':LabelProps.m_vColorSplits.emplace_back(a[i],(tmpf)?a[i+1]-a[i]:tmp_length-2*j-a[i],ColorRGBA(1.0f,0.0f,0.0f,1.0f));break;
					case 'a':LabelProps.m_vColorSplits.emplace_back(a[i],(tmpf)?a[i+1]-a[i]:tmp_length-2*j-a[i],ColorRGBA(0.6f,1.0f,0.6f,1.0f));break;
					case 'b':LabelProps.m_vColorSplits.emplace_back(a[i],(tmpf)?a[i+1]-a[i]:tmp_length-2*j-a[i],ColorRGBA(0.0f,1.0f,1.0f,1.0f));break;
					case 'd':LabelProps.m_vColorSplits.emplace_back(a[i],(tmpf)?a[i+1]-a[i]:tmp_length-2*j-a[i],ColorRGBA(1.0f,0.8f,1.0f,1.0f));break;
					case 'e':LabelProps.m_vColorSplits.emplace_back(a[i],(tmpf)?a[i+1]-a[i]:tmp_length-2*j-a[i],ColorRGBA(1.0f,1.0f,0.0f,1.0f));break;
					case 'f':LabelProps.m_vColorSplits.emplace_back(a[i],(tmpf)?a[i+1]-a[i]:tmp_length-2*j-a[i],ColorRGBA(1.0f,1.0f,1.0f,1.0f));break;
				}
			}
			int k=0;
			for(int i=0;i<tmp_length;i++)
			{
				if(*(b+i) == '%')
				{
					i++;
					continue;
				}
				e[k] = *(b+i);
				k++;
			}
		}
	}
	const char* manwhatcanisay = static_cast<const char*>(e);
	if(LabelData.m_Type == CButtonLabel::EType::RAINBOW || g_Config.m_ClLabelColorType == 2)
	{
		if(!m_pTouchControls->fknano)
		{
			m_pTouchControls->m_RainbowTimer = time_get_nanoseconds();
			m_pTouchControls->fknano = 1;
			m_pTouchControls->m_Rainbow = 0.0f;
		}
		if(time_get_nanoseconds() - m_pTouchControls->m_RainbowTimer >= static_cast<std::chrono::milliseconds>((int)(g_Config.m_ClLabelRainbowSpeed / 10.0f)))
		{
			m_pTouchControls->m_RainbowTimer = time_get_nanoseconds();
			m_pTouchControls->m_Rainbow += 1.0f;
		}
		float rainbownum = m_pTouchControls->m_Rainbow + (ctrx + ctry) / 2000000 * 600;
		m_pTouchControls->m_Rainbow = (m_pTouchControls->m_Rainbow >= 600.0f) ? m_pTouchControls->m_Rainbow - 600.0f:m_pTouchControls->m_Rainbow;
		rainbownum = (rainbownum >= 600.0f) ? rainbownum - 600.0f : rainbownum;
		LabelProps.m_vColorSplits.emplace_back(0,str_length(LabelData.m_pLabel),color_cast<ColorRGBA>(ColorHSLA(rainbownum / 600.0f,g_Config.m_ClLabelRainbowSat / 255.0f,g_Config.m_ClLabelRainbowLig / 255.0f, alpha)));
		j = 0;
	}
	if(LabelData.m_Type != CButtonLabel::EType::RAINBOW && g_Config.m_ClLabelColorType == 1)
	{
		ColorRGBA abcd = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClLabelColorStatic));
		abcd.a = alpha;
		LabelProps.m_vColorSplits.emplace_back(0,str_length(LabelData.m_pLabel),abcd);
	}
	if(LabelData.m_Type == CButtonLabel::EType::ICON)
	{
		m_pTouchControls->TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		m_pTouchControls->TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
		m_pTouchControls->Ui()->DoLabel(&LabelRect, LabelData.m_pLabel, FontSize, TEXTALIGN_MC, LabelProps);
		m_pTouchControls->TextRender()->SetRenderFlags(0);
		m_pTouchControls->TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	else
	{
		const char *pLabel = LabelData.m_Type == CButtonLabel::EType::LOCALIZED ? Localize(LabelData.m_pLabel) : LabelData.m_pLabel;
		m_pTouchControls->Ui()->DoLabel(&LabelRect, (j)?manwhatcanisay:pLabel, FontSize, TEXTALIGN_MC, LabelProps);
	}
}

void CTouchControls::CTouchButton::WriteToConfiguration(CJsonWriter *pWriter)
{
	char aBuf[256];

	pWriter->BeginObject();

	pWriter->WriteAttribute("x");
	pWriter->WriteIntValue(m_UnitRect.m_X);
	pWriter->WriteAttribute("y");
	pWriter->WriteIntValue(m_UnitRect.m_Y);
	pWriter->WriteAttribute("w");
	pWriter->WriteIntValue(m_UnitRect.m_W);
	pWriter->WriteAttribute("h");
	pWriter->WriteIntValue(m_UnitRect.m_H);

	pWriter->WriteAttribute("shape");
	pWriter->WriteStrValue(SHAPE_NAMES[(int)m_Shape]);

	pWriter->WriteAttribute("visibilities");
	pWriter->BeginArray();
	for(CButtonVisibility Visibility : m_vVisibilities)
	{
		str_format(aBuf, sizeof(aBuf), "%s%s", Visibility.m_Parity ? "" : "-", m_pTouchControls->m_aVisibilityFunctions[(int)Visibility.m_Type].m_pId);
		pWriter->WriteStrValue(aBuf);
	}
	pWriter->EndArray();

	pWriter->WriteAttribute("behavior");
	pWriter->BeginObject();
	m_pBehavior->WriteToConfiguration(pWriter);
	pWriter->EndObject();

	pWriter->EndObject();
}

void CTouchControls::CTouchButtonBehavior::Init(CTouchButton *pTouchButton)
{
	m_pTouchButton = pTouchButton;
	m_pTouchControls = pTouchButton->m_pTouchControls;
}

void CTouchControls::CTouchButtonBehavior::Reset()
{
	m_Active = false;
}

void CTouchControls::CTouchButtonBehavior::SetActive(const IInput::CTouchFingerState &FingerState)
{
	const vec2 ScreenSize = m_pTouchControls->CalculateScreenSize();
	const CUIRect ButtonScreenRect = m_pTouchButton->m_ScreenRect;
	const vec2 Position = (m_pTouchButton->ClampTouchPosition(FingerState.m_Position * ScreenSize) - ButtonScreenRect.TopLeft()) / ButtonScreenRect.Size();
	const vec2 Delta = FingerState.m_Delta * ScreenSize / ButtonScreenRect.Size();
	if(!m_Active)
	{
		m_Active = true;
		m_ActivePosition = Position;
		m_AccumulatedDelta = Delta;
		m_ActivationStartTime = time_get_nanoseconds();
		m_Finger = FingerState.m_Finger;
		OnActivate();
	}
	else if(m_Finger == FingerState.m_Finger)
	{
		m_ActivePosition = Position;
		m_AccumulatedDelta += Delta;
		OnUpdate();
	}
	else
	{
		dbg_assert(false, "Touch button must be inactive or use same finger");
	}
}

void CTouchControls::CTouchButtonBehavior::SetInactive()
{
	if(m_Active)
	{
		m_Active = false;
		OnDeactivate();
	}
}

bool CTouchControls::CTouchButtonBehavior::IsActive() const
{
	return m_Active;
}

bool CTouchControls::CTouchButtonBehavior::IsActive(const IInput::CTouchFinger &Finger) const
{
	return m_Active && m_Finger == Finger;
}

void CTouchControls::CPredefinedTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	pWriter->WriteAttribute("type");
	pWriter->WriteStrValue(BEHAVIOR_TYPE);

	pWriter->WriteAttribute("id");
	pWriter->WriteStrValue(m_pId);
}

// Ingame menu button: always opens ingame menu.
CTouchControls::CButtonLabel CTouchControls::CIngameMenuTouchButtonBehavior::GetLabel() const
{
	return {CButtonLabel::EType::ICON, "\xEF\x85\x8E"};
}

void CTouchControls::CIngameMenuTouchButtonBehavior::OnDeactivate()
{
	m_pTouchControls->GameClient()->m_Menus.SetActive(true);
}

// Extra menu button:
// - Short press: show/hide additional buttons (toggle extra-menu visibilities)
// - Long press: open ingame menu
CTouchControls::CExtraMenuTouchButtonBehavior::CExtraMenuTouchButtonBehavior(int Number) :
	CPredefinedTouchButtonBehavior(BEHAVIOR_ID),
	m_Number(Number)
{
	if(m_Number == 0)
	{
		str_copy(m_aLabel, "\xEF\x83\x89");
	}
	else
	{
		str_format(m_aLabel, sizeof(m_aLabel), "\xEF\x83\x89%d", m_Number + 1);
	}
}

CTouchControls::CButtonLabel CTouchControls::CExtraMenuTouchButtonBehavior::GetLabel() const
{
	if(m_Active && time_get_nanoseconds() - m_ActivationStartTime >= LONG_TOUCH_DURATION)
	{
		return {CButtonLabel::EType::ICON, "\xEF\x95\x90"};
	}
	else
	{
		return {CButtonLabel::EType::ICON, m_aLabel};
	}
}

void CTouchControls::CExtraMenuTouchButtonBehavior::OnDeactivate()
{
	if(time_get_nanoseconds() - m_ActivationStartTime >= LONG_TOUCH_DURATION)
	{
		m_pTouchControls->GameClient()->m_Menus.SetActive(true);
	}
	else
	{
		m_pTouchControls->m_aExtraMenuActive[m_Number] = !m_pTouchControls->m_aExtraMenuActive[m_Number];
	}
}

void CTouchControls::CExtraMenuTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	CPredefinedTouchButtonBehavior::WriteToConfiguration(pWriter);

	pWriter->WriteAttribute("number");
	pWriter->WriteIntValue(m_Number + 1);
}

// Close All Extra Menus button : You know what I mean.
CTouchControls::CButtonLabel CTouchControls::CCloseAllExtraMenuTouchButtonBehavior::GetLabel() const
{
	return {CButtonLabel::EType::PLAIN, "Reset"};
}
void CTouchControls::CCloseAllExtraMenuTouchButtonBehavior::OnDeactivate()
{
	for(int tmp_i = 0;tmp_i < 999;tmp_i++)
	m_pTouchControls->m_aExtraMenuActive[tmp_i] = false;
}

// Emoticon button: keeps the emoticon HUD open, next touch in emoticon HUD will close it again.
CTouchControls::CButtonLabel CTouchControls::CEmoticonTouchButtonBehavior::GetLabel() const
{
	return {CButtonLabel::EType::LOCALIZED, Localizable("Emoticon")};
}

void CTouchControls::CEmoticonTouchButtonBehavior::OnDeactivate()
{
	m_pTouchControls->Console()->ExecuteLineStroked(1, "+emote");
}

// Spectate button: keeps the spectate menu open, next touch in spectate menu will close it again.
CTouchControls::CButtonLabel CTouchControls::CSpectateTouchButtonBehavior::GetLabel() const
{
	return {CButtonLabel::EType::LOCALIZED, Localizable("Spectator mode")};
}

void CTouchControls::CSpectateTouchButtonBehavior::OnDeactivate()
{
	m_pTouchControls->Console()->ExecuteLineStroked(1, "+spectate");
}

// Swap action button:
// - If joystick is currently active with one action: activate the other action.
// - Else: swap active action.
CTouchControls::CButtonLabel CTouchControls::CSwapActionTouchButtonBehavior::GetLabel() const
{
	if(m_ActiveAction != NUM_ACTIONS)
	{
		return {CButtonLabel::EType::LOCALIZED, ACTION_NAMES[m_ActiveAction]};
	}
	else if(m_pTouchControls->m_pPrimaryJoystickTouchButtonBehavior != nullptr &&
		m_pTouchControls->m_pPrimaryJoystickTouchButtonBehavior->ActiveAction() != NUM_ACTIONS)
	{
		return {CButtonLabel::EType::LOCALIZED, ACTION_NAMES[m_pTouchControls->NextActiveAction(m_pTouchControls->m_pPrimaryJoystickTouchButtonBehavior->ActiveAction())]};
	}
	return {CButtonLabel::EType::LOCALIZED, ACTION_SWAP_NAMES[m_pTouchControls->m_ActionSelected]};
}

void CTouchControls::CSwapActionTouchButtonBehavior::OnActivate()
{
	if(m_pTouchControls->m_pPrimaryJoystickTouchButtonBehavior != nullptr &&
		m_pTouchControls->m_pPrimaryJoystickTouchButtonBehavior->ActiveAction() != NUM_ACTIONS)
	{
		m_ActiveAction = m_pTouchControls->NextActiveAction(m_pTouchControls->m_pPrimaryJoystickTouchButtonBehavior->ActiveAction());
		m_pTouchControls->Console()->ExecuteLineStroked(1, ACTION_COMMANDS[m_ActiveAction]);
	}
	else
	{
		m_pTouchControls->m_ActionSelected = m_pTouchControls->NextActiveAction(m_pTouchControls->m_ActionSelected);
	}
}

void CTouchControls::CSwapActionTouchButtonBehavior::OnDeactivate()
{
	if(m_ActiveAction != NUM_ACTIONS)
	{
		m_pTouchControls->Console()->ExecuteLineStroked(0, ACTION_COMMANDS[m_ActiveAction]);
		m_ActiveAction = NUM_ACTIONS;
	}
}

// Use action button: always uses the active action.
CTouchControls::CButtonLabel CTouchControls::CUseActionTouchButtonBehavior::GetLabel() const
{
	if(m_ActiveAction != NUM_ACTIONS)
	{
		return {CButtonLabel::EType::LOCALIZED, ACTION_NAMES[m_ActiveAction]};
	}
	return {CButtonLabel::EType::LOCALIZED, ACTION_NAMES[m_pTouchControls->m_ActionSelected]};
}

void CTouchControls::CUseActionTouchButtonBehavior::OnActivate()
{
	m_ActiveAction = m_pTouchControls->m_ActionSelected;
	m_pTouchControls->Console()->ExecuteLineStroked(1, ACTION_COMMANDS[m_ActiveAction]);
}

void CTouchControls::CUseActionTouchButtonBehavior::OnDeactivate()
{
	m_pTouchControls->Console()->ExecuteLineStroked(0, ACTION_COMMANDS[m_ActiveAction]);
	m_ActiveAction = NUM_ACTIONS;
}

// Generic joystick button behavior: aim with virtual joystick and use action (defined by subclass).
CTouchControls::CButtonLabel CTouchControls::CJoystickTouchButtonBehavior::GetLabel() const
{
	if(m_ActiveAction != NUM_ACTIONS)
	{
		return {CButtonLabel::EType::LOCALIZED, ACTION_NAMES[m_ActiveAction]};
	}
	return {CButtonLabel::EType::LOCALIZED, ACTION_NAMES[SelectedAction()]};
}

void CTouchControls::CJoystickTouchButtonBehavior::OnActivate()
{
	m_ActiveAction = SelectedAction();
	OnUpdate();
	if(m_ActiveAction != ACTION_AIM)
	{
		m_pTouchControls->Console()->ExecuteLineStroked(1, ACTION_COMMANDS[m_ActiveAction]);
	}
}

void CTouchControls::CJoystickTouchButtonBehavior::OnDeactivate()
{
	if(m_ActiveAction != ACTION_AIM)
	{
		m_pTouchControls->Console()->ExecuteLineStroked(0, ACTION_COMMANDS[m_ActiveAction]);
	}
	m_ActiveAction = NUM_ACTIONS;
}

void CTouchControls::CJoystickTouchButtonBehavior::OnUpdate()
{
	CControls &Controls = m_pTouchControls->GameClient()->m_Controls;
	if(m_pTouchControls->GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		vec2 WorldScreenSize;
		m_pTouchControls->RenderTools()->CalcScreenParams(m_pTouchControls->Graphics()->ScreenAspect(), m_pTouchControls->GameClient()->m_Camera.m_Zoom, &WorldScreenSize.x, &WorldScreenSize.y);
		Controls.m_aMousePos[g_Config.m_ClDummy] += -m_AccumulatedDelta * WorldScreenSize;
		Controls.m_aMousePos[g_Config.m_ClDummy].x = clamp(Controls.m_aMousePos[g_Config.m_ClDummy].x, -201.0f * 32, (m_pTouchControls->Collision()->GetWidth() + 201.0f) * 32.0f);
		Controls.m_aMousePos[g_Config.m_ClDummy].y = clamp(Controls.m_aMousePos[g_Config.m_ClDummy].y, -201.0f * 32, (m_pTouchControls->Collision()->GetHeight() + 201.0f) * 32.0f);
		m_AccumulatedDelta = vec2(0.0f, 0.0f);
	}
	else
	{
		const vec2 AbsolutePosition = (m_ActivePosition - vec2(0.5f, 0.5f)) * 2.0f;
		Controls.m_aMousePos[g_Config.m_ClDummy] = AbsolutePosition * (Controls.GetMaxMouseDistance() - Controls.GetMinMouseDistance()) + normalize(AbsolutePosition) * Controls.GetMinMouseDistance();
		if(length(Controls.m_aMousePos[g_Config.m_ClDummy]) < 0.001f)
		{
			Controls.m_aMousePos[g_Config.m_ClDummy].x = 0.001f;
			Controls.m_aMousePos[g_Config.m_ClDummy].y = 0.0f;
		}
	}
}

// Joystick that uses the active action. Registers itself as the primary joystick.
void CTouchControls::CJoystickActionTouchButtonBehavior::Init(CTouchButton *pTouchButton)
{
	CPredefinedTouchButtonBehavior::Init(pTouchButton);
	m_pTouchControls->m_pPrimaryJoystickTouchButtonBehavior = this;
}

int CTouchControls::CJoystickActionTouchButtonBehavior::SelectedAction() const
{
	return m_pTouchControls->m_ActionSelected;
}

// Joystick that only aims.
int CTouchControls::CJoystickAimTouchButtonBehavior::SelectedAction() const
{
	return ACTION_AIM;
}

// Joystick that always uses fire.
int CTouchControls::CJoystickFireTouchButtonBehavior::SelectedAction() const
{
	return ACTION_FIRE;
}

// Joystick that always uses hook.
int CTouchControls::CJoystickHookTouchButtonBehavior::SelectedAction() const
{
	return ACTION_HOOK;
}

// Bind button behavior that executes a command like a bind.
CTouchControls::CButtonLabel CTouchControls::CBindTouchButtonBehavior::GetLabel() const
{
	return {m_LabelType, m_Label.c_str()};
}

void CTouchControls::CBindTouchButtonBehavior::OnActivate()
{
	m_pTouchControls->Console()->ExecuteLineStroked(1, m_Command.c_str());
	m_Repeating = false;
}

void CTouchControls::CBindTouchButtonBehavior::OnDeactivate()
{
	m_pTouchControls->Console()->ExecuteLineStroked(0, m_Command.c_str());
}

void CTouchControls::CBindTouchButtonBehavior::OnUpdate()
{
	const auto Now = time_get_nanoseconds();
	if(m_Repeating)
	{
		m_AccumulatedRepeatingTime += Now - m_LastUpdateTime;
		m_LastUpdateTime = Now;
		if(m_AccumulatedRepeatingTime >= BIND_REPEAT_RATE)
		{
			m_AccumulatedRepeatingTime -= BIND_REPEAT_RATE;
			m_pTouchControls->Console()->ExecuteLineStroked(1, m_Command.c_str());
		}
	}
	else if(Now - m_ActivationStartTime >= BIND_REPEAT_INITIAL_DELAY)
	{
		m_Repeating = true;
		m_LastUpdateTime = Now;
		m_AccumulatedRepeatingTime = 0ns;
	}
}

void CTouchControls::CBindTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	pWriter->WriteAttribute("type");
	pWriter->WriteStrValue(BEHAVIOR_TYPE);

	pWriter->WriteAttribute("label");
	pWriter->WriteStrValue(m_Label.c_str());

	pWriter->WriteAttribute("label-type");
	pWriter->WriteStrValue(LABEL_TYPE_NAMES[(int)m_LabelType]);

	pWriter->WriteAttribute("command");
	pWriter->WriteStrValue(m_Command.c_str());
}

// Bind button behavior that switches between executing one of two or more console commands.
CTouchControls::CButtonLabel CTouchControls::CBindToggleTouchButtonBehavior::GetLabel() const
{
	const auto &ActiveCommand = m_vCommands[m_ActiveCommandIndex];
	return {ActiveCommand.m_LabelType, ActiveCommand.m_Label.c_str()};
}

void CTouchControls::CBindToggleTouchButtonBehavior::OnActivate()
{
	m_pTouchControls->Console()->ExecuteLine(m_vCommands[m_ActiveCommandIndex].m_Command.c_str());
	m_ActiveCommandIndex = (m_ActiveCommandIndex + 1) % m_vCommands.size();
}

void CTouchControls::CBindToggleTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	pWriter->WriteAttribute("type");
	pWriter->WriteStrValue(BEHAVIOR_TYPE);

	pWriter->WriteAttribute("commands");
	pWriter->BeginArray();

	for(const auto &Command : m_vCommands)
	{
		pWriter->BeginObject();

		pWriter->WriteAttribute("label");
		pWriter->WriteStrValue(Command.m_Label.c_str());

		pWriter->WriteAttribute("label-type");
		pWriter->WriteStrValue(LABEL_TYPE_NAMES[(int)Command.m_LabelType]);

		pWriter->WriteAttribute("command");
		pWriter->WriteStrValue(Command.m_Command.c_str());

		pWriter->EndObject();
	}

	pWriter->EndArray();
}

void CTouchControls::OnInit()
{
	InitVisibilityFunctions();
	if(!LoadConfigurationFromFile(IStorage::TYPE_ALL))
	{
		Client()->AddWarning(SWarning(Localize("Error loading touch controls"), Localize("Could not load touch controls from file. See local console for details.")));
	}
}

void CTouchControls::OnReset()
{
	ResetButtons();
	m_EditingActive = false;
}

void CTouchControls::OnWindowResize()
{
	ResetButtons();
	for(CTouchButton &TouchButton : m_vTouchButtons)
	{
		TouchButton.UpdateScreenFromUnitRect();
	}
}

bool CTouchControls::OnTouchState(const std::vector<IInput::CTouchFingerState> &vTouchFingerStates)
{
	if(!g_Config.m_ClTouchControls)
		return false;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	if(GameClient()->m_Chat.IsActive() ||
		GameClient()->m_GameConsole.IsActive() ||
		GameClient()->m_Menus.IsActive() ||
		GameClient()->m_Emoticon.IsActive() ||
		GameClient()->m_Spectator.IsActive())
	{
		ResetButtons();
		return false;
	}

	UpdateButtons(vTouchFingerStates);
	return true;
}

void CTouchControls::OnRender()
{
	if(!g_Config.m_ClTouchControls)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(GameClient()->m_Chat.IsActive() ||
		GameClient()->m_Emoticon.IsActive() ||
		GameClient()->m_Spectator.IsActive())
	{
		return;
	}

	const vec2 ScreenSize = CalculateScreenSize();
	Graphics()->MapScreen(0.0f, 0.0f, ScreenSize.x, ScreenSize.y);

	RenderButtons();
}

bool CTouchControls::LoadConfigurationFromFile(int StorageType)
{
	void *pFileData;
	unsigned FileLength;
	if(!Storage()->ReadFile(CONFIGURATION_FILENAME, StorageType, &pFileData, &FileLength))
	{
		log_error("touch_controls", "Failed to read configuration from '%s'", CONFIGURATION_FILENAME);
		return false;
	}

	const bool Result = ParseConfiguration(pFileData, FileLength);
	free(pFileData);
	return Result;
}

bool CTouchControls::LoadConfigurationFromClipboard()
{
	std::string Clipboard = Input()->GetClipboardText();
	return ParseConfiguration(Clipboard.c_str(), Clipboard.size());
}

bool CTouchControls::SaveConfigurationToFile()
{
	IOHANDLE File = Storage()->OpenFile(CONFIGURATION_FILENAME, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		log_error("touch_controls", "Failed to open '%s' for writing configuration", CONFIGURATION_FILENAME);
		return false;
	}

	CJsonFileWriter Writer(File);
	WriteConfiguration(&Writer);
	return true;
}

void CTouchControls::SaveConfigurationToClipboard()
{
	CJsonStringWriter Writer;
	WriteConfiguration(&Writer);
	std::string ConfigurationString = Writer.GetOutputString();
	Input()->SetClipboardText(ConfigurationString.c_str());
}

void CTouchControls::InitVisibilityFunctions()
{
	m_aVisibilityFunctions[(int)EButtonVisibility::INGAME].m_pId = "ingame";
	m_aVisibilityFunctions[(int)EButtonVisibility::INGAME].m_Function = [&]() {
		return !GameClient()->m_Snap.m_SpecInfo.m_Active;
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::ZOOM_ALLOWED].m_pId = "zoom-allowed";
	m_aVisibilityFunctions[(int)EButtonVisibility::ZOOM_ALLOWED].m_Function = [&]() {
		return GameClient()->m_Camera.ZoomAllowed();
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::VOTE_ACTIVE].m_pId = "vote-active";
	m_aVisibilityFunctions[(int)EButtonVisibility::VOTE_ACTIVE].m_Function = [&]() {
		return GameClient()->m_Voting.IsVoting();
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::DUMMY_ALLOWED].m_pId = "dummy-allowed";
	m_aVisibilityFunctions[(int)EButtonVisibility::DUMMY_ALLOWED].m_Function = [&]() {
		return Client()->DummyAllowed();
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::DUMMY_CONNECTED].m_pId = "dummy-connected";
	m_aVisibilityFunctions[(int)EButtonVisibility::DUMMY_CONNECTED].m_Function = [&]() {
		return Client()->DummyConnected();
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::RCON_AUTHED].m_pId = "rcon-authed";
	m_aVisibilityFunctions[(int)EButtonVisibility::RCON_AUTHED].m_Function = [&]() {
		return Client()->RconAuthed();
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::DEMO_PLAYER].m_pId = "demo-player";
	m_aVisibilityFunctions[(int)EButtonVisibility::DEMO_PLAYER].m_Function = [&]() {
		return Client()->State() == IClient::STATE_DEMOPLAYBACK;
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_1].m_pId = "extra-menu";
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_1].m_Function = [&]() {
		return m_aExtraMenuActive[0];
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_2].m_pId = "extra-menu-2";
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_2].m_Function = [&]() {
		return m_aExtraMenuActive[1];
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_3].m_pId = "extra-menu-3";
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_3].m_Function = [&]() {
		return m_aExtraMenuActive[2];
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_4].m_pId = "extra-menu-4";
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_4].m_Function = [&]() {
		return m_aExtraMenuActive[3];
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_5].m_pId = "extra-menu-5";
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_5].m_Function = [&]() {
		return m_aExtraMenuActive[4];
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_6].m_pId = "extra-menu-6";
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_6].m_Function = [&]() {
		return m_aExtraMenuActive[5];
	};
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_7].m_pId = "extra-menu-7";
	m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_7].m_Function = [&]() {
		return m_aExtraMenuActive[6];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_8].m_pId = "extra-menu-8";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_8].m_Function = [&]() {
        return m_aExtraMenuActive[7];
    };
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_9].m_pId = "extra-menu-9";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_9].m_Function = [&]() {
        return m_aExtraMenuActive[8];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_10].m_pId = "extra-menu-10";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_10].m_Function = [&]() {
        return m_aExtraMenuActive[9];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_11].m_pId = "extra-menu-11";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_11].m_Function = [&]() {
        return m_aExtraMenuActive[10];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_12].m_pId = "extra-menu-12";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_12].m_Function = [&]() {
        return m_aExtraMenuActive[11];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_13].m_pId = "extra-menu-13";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_13].m_Function = [&]() {
        return m_aExtraMenuActive[12];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_14].m_pId = "extra-menu-14";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_14].m_Function = [&]() {
        return m_aExtraMenuActive[13];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_15].m_pId = "extra-menu-15";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_15].m_Function = [&]() {
        return m_aExtraMenuActive[14];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_16].m_pId = "extra-menu-16";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_16].m_Function = [&]() {
        return m_aExtraMenuActive[15];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_17].m_pId = "extra-menu-17";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_17].m_Function = [&]() {
        return m_aExtraMenuActive[16];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_18].m_pId = "extra-menu-18";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_18].m_Function = [&]() {
        return m_aExtraMenuActive[17];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_19].m_pId = "extra-menu-19";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_19].m_Function = [&]() {
        return m_aExtraMenuActive[18];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_20].m_pId = "extra-menu-20";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_20].m_Function = [&]() {
        return m_aExtraMenuActive[19];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_21].m_pId = "extra-menu-21";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_21].m_Function = [&]() {
        return m_aExtraMenuActive[20];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_22].m_pId = "extra-menu-22";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_22].m_Function = [&]() {
        return m_aExtraMenuActive[21];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_23].m_pId = "extra-menu-23";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_23].m_Function = [&]() {
        return m_aExtraMenuActive[22];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_24].m_pId = "extra-menu-24";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_24].m_Function = [&]() {
        return m_aExtraMenuActive[23];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_25].m_pId = "extra-menu-25";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_25].m_Function = [&]() {
        return m_aExtraMenuActive[24];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_26].m_pId = "extra-menu-26";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_26].m_Function = [&]() {
        return m_aExtraMenuActive[25];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_27].m_pId = "extra-menu-27";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_27].m_Function = [&]() {
        return m_aExtraMenuActive[26];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_28].m_pId = "extra-menu-28";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_28].m_Function = [&]() {
        return m_aExtraMenuActive[27];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_29].m_pId = "extra-menu-29";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_29].m_Function = [&]() {
        return m_aExtraMenuActive[28];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_30].m_pId = "extra-menu-30";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_30].m_Function = [&]() {
        return m_aExtraMenuActive[29];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_31].m_pId = "extra-menu-31";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_31].m_Function = [&]() {
        return m_aExtraMenuActive[30];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_32].m_pId = "extra-menu-32";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_32].m_Function = [&]() {
        return m_aExtraMenuActive[31];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_33].m_pId = "extra-menu-33";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_33].m_Function = [&]() {
        return m_aExtraMenuActive[32];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_34].m_pId = "extra-menu-34";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_34].m_Function = [&]() {
        return m_aExtraMenuActive[33];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_35].m_pId = "extra-menu-35";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_35].m_Function = [&]() {
        return m_aExtraMenuActive[34];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_36].m_pId = "extra-menu-36";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_36].m_Function = [&]() {
        return m_aExtraMenuActive[35];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_37].m_pId = "extra-menu-37";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_37].m_Function = [&]() {
        return m_aExtraMenuActive[36];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_38].m_pId = "extra-menu-38";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_38].m_Function = [&]() {
        return m_aExtraMenuActive[37];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_39].m_pId = "extra-menu-39";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_39].m_Function = [&]() {
        return m_aExtraMenuActive[38];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_40].m_pId = "extra-menu-40";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_40].m_Function = [&]() {
        return m_aExtraMenuActive[39];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_41].m_pId = "extra-menu-41";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_41].m_Function = [&]() {
        return m_aExtraMenuActive[40];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_42].m_pId = "extra-menu-42";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_42].m_Function = [&]() {
        return m_aExtraMenuActive[41];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_43].m_pId = "extra-menu-43";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_43].m_Function = [&]() {
        return m_aExtraMenuActive[42];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_44].m_pId = "extra-menu-44";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_44].m_Function = [&]() {
        return m_aExtraMenuActive[43];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_45].m_pId = "extra-menu-45";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_45].m_Function = [&]() {
        return m_aExtraMenuActive[44];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_46].m_pId = "extra-menu-46";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_46].m_Function = [&]() {
        return m_aExtraMenuActive[45];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_47].m_pId = "extra-menu-47";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_47].m_Function = [&]() {
        return m_aExtraMenuActive[46];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_48].m_pId = "extra-menu-48";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_48].m_Function = [&]() {
        return m_aExtraMenuActive[47];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_49].m_pId = "extra-menu-49";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_49].m_Function = [&]() {
        return m_aExtraMenuActive[48];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_50].m_pId = "extra-menu-50";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_50].m_Function = [&]() {
        return m_aExtraMenuActive[49];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_51].m_pId = "extra-menu-51";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_51].m_Function = [&]() {
        return m_aExtraMenuActive[50];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_52].m_pId = "extra-menu-52";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_52].m_Function = [&]() {
        return m_aExtraMenuActive[51];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_53].m_pId = "extra-menu-53";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_53].m_Function = [&]() {
        return m_aExtraMenuActive[52];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_54].m_pId = "extra-menu-54";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_54].m_Function = [&]() {
        return m_aExtraMenuActive[53];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_55].m_pId = "extra-menu-55";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_55].m_Function = [&]() {
        return m_aExtraMenuActive[54];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_56].m_pId = "extra-menu-56";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_56].m_Function = [&]() {
        return m_aExtraMenuActive[55];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_57].m_pId = "extra-menu-57";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_57].m_Function = [&]() {
        return m_aExtraMenuActive[56];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_58].m_pId = "extra-menu-58";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_58].m_Function = [&]() {
        return m_aExtraMenuActive[57];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_59].m_pId = "extra-menu-59";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_59].m_Function = [&]() {
        return m_aExtraMenuActive[58];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_60].m_pId = "extra-menu-60";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_60].m_Function = [&]() {
        return m_aExtraMenuActive[59];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_61].m_pId = "extra-menu-61";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_61].m_Function = [&]() {
        return m_aExtraMenuActive[60];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_62].m_pId = "extra-menu-62";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_62].m_Function = [&]() {
        return m_aExtraMenuActive[61];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_63].m_pId = "extra-menu-63";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_63].m_Function = [&]() {
        return m_aExtraMenuActive[62];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_64].m_pId = "extra-menu-64";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_64].m_Function = [&]() {
        return m_aExtraMenuActive[63];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_65].m_pId = "extra-menu-65";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_65].m_Function = [&]() {
        return m_aExtraMenuActive[64];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_66].m_pId = "extra-menu-66";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_66].m_Function = [&]() {
        return m_aExtraMenuActive[65];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_67].m_pId = "extra-menu-67";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_67].m_Function = [&]() {
        return m_aExtraMenuActive[66];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_68].m_pId = "extra-menu-68";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_68].m_Function = [&]() {
        return m_aExtraMenuActive[67];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_69].m_pId = "extra-menu-69";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_69].m_Function = [&]() {
        return m_aExtraMenuActive[68];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_70].m_pId = "extra-menu-70";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_70].m_Function = [&]() {
        return m_aExtraMenuActive[69];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_71].m_pId = "extra-menu-71";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_71].m_Function = [&]() {
        return m_aExtraMenuActive[70];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_72].m_pId = "extra-menu-72";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_72].m_Function = [&]() {
        return m_aExtraMenuActive[71];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_73].m_pId = "extra-menu-73";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_73].m_Function = [&]() {
        return m_aExtraMenuActive[72];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_74].m_pId = "extra-menu-74";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_74].m_Function = [&]() {
        return m_aExtraMenuActive[73];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_75].m_pId = "extra-menu-75";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_75].m_Function = [&]() {
        return m_aExtraMenuActive[74];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_76].m_pId = "extra-menu-76";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_76].m_Function = [&]() {
        return m_aExtraMenuActive[75];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_77].m_pId = "extra-menu-77";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_77].m_Function = [&]() {
        return m_aExtraMenuActive[76];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_78].m_pId = "extra-menu-78";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_78].m_Function = [&]() {
        return m_aExtraMenuActive[77];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_79].m_pId = "extra-menu-79";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_79].m_Function = [&]() {
        return m_aExtraMenuActive[78];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_80].m_pId = "extra-menu-80";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_80].m_Function = [&]() {
        return m_aExtraMenuActive[79];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_81].m_pId = "extra-menu-81";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_81].m_Function = [&]() {
        return m_aExtraMenuActive[80];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_82].m_pId = "extra-menu-82";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_82].m_Function = [&]() {
        return m_aExtraMenuActive[81];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_83].m_pId = "extra-menu-83";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_83].m_Function = [&]() {
        return m_aExtraMenuActive[82];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_84].m_pId = "extra-menu-84";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_84].m_Function = [&]() {
        return m_aExtraMenuActive[83];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_85].m_pId = "extra-menu-85";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_85].m_Function = [&]() {
        return m_aExtraMenuActive[84];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_86].m_pId = "extra-menu-86";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_86].m_Function = [&]() {
        return m_aExtraMenuActive[85];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_87].m_pId = "extra-menu-87";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_87].m_Function = [&]() {
        return m_aExtraMenuActive[86];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_88].m_pId = "extra-menu-88";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_88].m_Function = [&]() {
        return m_aExtraMenuActive[87];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_89].m_pId = "extra-menu-89";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_89].m_Function = [&]() {
        return m_aExtraMenuActive[88];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_90].m_pId = "extra-menu-90";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_90].m_Function = [&]() {
        return m_aExtraMenuActive[89];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_91].m_pId = "extra-menu-91";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_91].m_Function = [&]() {
        return m_aExtraMenuActive[90];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_92].m_pId = "extra-menu-92";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_92].m_Function = [&]() {
        return m_aExtraMenuActive[91];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_93].m_pId = "extra-menu-93";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_93].m_Function = [&]() {
        return m_aExtraMenuActive[92];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_94].m_pId = "extra-menu-94";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_94].m_Function = [&]() {
        return m_aExtraMenuActive[93];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_95].m_pId = "extra-menu-95";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_95].m_Function = [&]() {
        return m_aExtraMenuActive[94];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_96].m_pId = "extra-menu-96";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_96].m_Function = [&]() {
        return m_aExtraMenuActive[95];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_97].m_pId = "extra-menu-97";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_97].m_Function = [&]() {
        return m_aExtraMenuActive[96];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_98].m_pId = "extra-menu-98";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_98].m_Function = [&]() {
        return m_aExtraMenuActive[97];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_99].m_pId = "extra-menu-99";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_99].m_Function = [&]() {
        return m_aExtraMenuActive[98];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_100].m_pId = "extra-menu-100";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_100].m_Function = [&]() {
        return m_aExtraMenuActive[99];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_101].m_pId = "extra-menu-101";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_101].m_Function = [&]() {
        return m_aExtraMenuActive[100];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_102].m_pId = "extra-menu-102";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_102].m_Function = [&]() {
        return m_aExtraMenuActive[101];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_103].m_pId = "extra-menu-103";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_103].m_Function = [&]() {
        return m_aExtraMenuActive[102];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_104].m_pId = "extra-menu-104";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_104].m_Function = [&]() {
        return m_aExtraMenuActive[103];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_105].m_pId = "extra-menu-105";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_105].m_Function = [&]() {
        return m_aExtraMenuActive[104];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_106].m_pId = "extra-menu-106";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_106].m_Function = [&]() {
        return m_aExtraMenuActive[105];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_107].m_pId = "extra-menu-107";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_107].m_Function = [&]() {
        return m_aExtraMenuActive[106];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_108].m_pId = "extra-menu-108";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_108].m_Function = [&]() {
        return m_aExtraMenuActive[107];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_109].m_pId = "extra-menu-109";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_109].m_Function = [&]() {
        return m_aExtraMenuActive[108];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_110].m_pId = "extra-menu-110";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_110].m_Function = [&]() {
        return m_aExtraMenuActive[109];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_111].m_pId = "extra-menu-111";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_111].m_Function = [&]() {
        return m_aExtraMenuActive[110];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_112].m_pId = "extra-menu-112";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_112].m_Function = [&]() {
        return m_aExtraMenuActive[111];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_113].m_pId = "extra-menu-113";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_113].m_Function = [&]() {
        return m_aExtraMenuActive[112];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_114].m_pId = "extra-menu-114";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_114].m_Function = [&]() {
        return m_aExtraMenuActive[113];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_115].m_pId = "extra-menu-115";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_115].m_Function = [&]() {
        return m_aExtraMenuActive[114];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_116].m_pId = "extra-menu-116";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_116].m_Function = [&]() {
        return m_aExtraMenuActive[115];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_117].m_pId = "extra-menu-117";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_117].m_Function = [&]() {
        return m_aExtraMenuActive[116];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_118].m_pId = "extra-menu-118";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_118].m_Function = [&]() {
        return m_aExtraMenuActive[117];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_119].m_pId = "extra-menu-119";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_119].m_Function = [&]() {
        return m_aExtraMenuActive[118];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_120].m_pId = "extra-menu-120";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_120].m_Function = [&]() {
        return m_aExtraMenuActive[119];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_121].m_pId = "extra-menu-121";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_121].m_Function = [&]() {
        return m_aExtraMenuActive[120];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_122].m_pId = "extra-menu-122";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_122].m_Function = [&]() {
        return m_aExtraMenuActive[121];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_123].m_pId = "extra-menu-123";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_123].m_Function = [&]() {
        return m_aExtraMenuActive[122];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_124].m_pId = "extra-menu-124";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_124].m_Function = [&]() {
        return m_aExtraMenuActive[123];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_125].m_pId = "extra-menu-125";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_125].m_Function = [&]() {
        return m_aExtraMenuActive[124];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_126].m_pId = "extra-menu-126";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_126].m_Function = [&]() {
        return m_aExtraMenuActive[125];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_127].m_pId = "extra-menu-127";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_127].m_Function = [&]() {
        return m_aExtraMenuActive[126];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_128].m_pId = "extra-menu-128";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_128].m_Function = [&]() {
        return m_aExtraMenuActive[127];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_129].m_pId = "extra-menu-129";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_129].m_Function = [&]() {
        return m_aExtraMenuActive[128];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_130].m_pId = "extra-menu-130";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_130].m_Function = [&]() {
        return m_aExtraMenuActive[129];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_131].m_pId = "extra-menu-131";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_131].m_Function = [&]() {
        return m_aExtraMenuActive[130];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_132].m_pId = "extra-menu-132";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_132].m_Function = [&]() {
        return m_aExtraMenuActive[131];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_133].m_pId = "extra-menu-133";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_133].m_Function = [&]() {
        return m_aExtraMenuActive[132];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_134].m_pId = "extra-menu-134";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_134].m_Function = [&]() {
        return m_aExtraMenuActive[133];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_135].m_pId = "extra-menu-135";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_135].m_Function = [&]() {
        return m_aExtraMenuActive[134];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_136].m_pId = "extra-menu-136";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_136].m_Function = [&]() {
        return m_aExtraMenuActive[135];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_137].m_pId = "extra-menu-137";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_137].m_Function = [&]() {
        return m_aExtraMenuActive[136];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_138].m_pId = "extra-menu-138";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_138].m_Function = [&]() {
        return m_aExtraMenuActive[137];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_139].m_pId = "extra-menu-139";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_139].m_Function = [&]() {
        return m_aExtraMenuActive[138];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_140].m_pId = "extra-menu-140";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_140].m_Function = [&]() {
        return m_aExtraMenuActive[139];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_141].m_pId = "extra-menu-141";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_141].m_Function = [&]() {
        return m_aExtraMenuActive[140];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_142].m_pId = "extra-menu-142";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_142].m_Function = [&]() {
        return m_aExtraMenuActive[141];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_143].m_pId = "extra-menu-143";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_143].m_Function = [&]() {
        return m_aExtraMenuActive[142];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_144].m_pId = "extra-menu-144";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_144].m_Function = [&]() {
        return m_aExtraMenuActive[143];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_145].m_pId = "extra-menu-145";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_145].m_Function = [&]() {
        return m_aExtraMenuActive[144];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_146].m_pId = "extra-menu-146";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_146].m_Function = [&]() {
        return m_aExtraMenuActive[145];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_147].m_pId = "extra-menu-147";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_147].m_Function = [&]() {
        return m_aExtraMenuActive[146];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_148].m_pId = "extra-menu-148";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_148].m_Function = [&]() {
        return m_aExtraMenuActive[147];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_149].m_pId = "extra-menu-149";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_149].m_Function = [&]() {
        return m_aExtraMenuActive[148];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_150].m_pId = "extra-menu-150";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_150].m_Function = [&]() {
        return m_aExtraMenuActive[149];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_151].m_pId = "extra-menu-151";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_151].m_Function = [&]() {
        return m_aExtraMenuActive[150];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_152].m_pId = "extra-menu-152";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_152].m_Function = [&]() {
        return m_aExtraMenuActive[151];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_153].m_pId = "extra-menu-153";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_153].m_Function = [&]() {
        return m_aExtraMenuActive[152];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_154].m_pId = "extra-menu-154";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_154].m_Function = [&]() {
        return m_aExtraMenuActive[153];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_155].m_pId = "extra-menu-155";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_155].m_Function = [&]() {
        return m_aExtraMenuActive[154];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_156].m_pId = "extra-menu-156";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_156].m_Function = [&]() {
        return m_aExtraMenuActive[155];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_157].m_pId = "extra-menu-157";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_157].m_Function = [&]() {
        return m_aExtraMenuActive[156];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_158].m_pId = "extra-menu-158";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_158].m_Function = [&]() {
        return m_aExtraMenuActive[157];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_159].m_pId = "extra-menu-159";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_159].m_Function = [&]() {
        return m_aExtraMenuActive[158];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_160].m_pId = "extra-menu-160";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_160].m_Function = [&]() {
        return m_aExtraMenuActive[159];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_161].m_pId = "extra-menu-161";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_161].m_Function = [&]() {
        return m_aExtraMenuActive[160];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_162].m_pId = "extra-menu-162";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_162].m_Function = [&]() {
        return m_aExtraMenuActive[161];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_163].m_pId = "extra-menu-163";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_163].m_Function = [&]() {
        return m_aExtraMenuActive[162];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_164].m_pId = "extra-menu-164";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_164].m_Function = [&]() {
        return m_aExtraMenuActive[163];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_165].m_pId = "extra-menu-165";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_165].m_Function = [&]() {
        return m_aExtraMenuActive[164];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_166].m_pId = "extra-menu-166";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_166].m_Function = [&]() {
        return m_aExtraMenuActive[165];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_167].m_pId = "extra-menu-167";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_167].m_Function = [&]() {
        return m_aExtraMenuActive[166];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_168].m_pId = "extra-menu-168";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_168].m_Function = [&]() {
        return m_aExtraMenuActive[167];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_169].m_pId = "extra-menu-169";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_169].m_Function = [&]() {
        return m_aExtraMenuActive[168];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_170].m_pId = "extra-menu-170";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_170].m_Function = [&]() {
        return m_aExtraMenuActive[169];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_171].m_pId = "extra-menu-171";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_171].m_Function = [&]() {
        return m_aExtraMenuActive[170];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_172].m_pId = "extra-menu-172";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_172].m_Function = [&]() {
        return m_aExtraMenuActive[171];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_173].m_pId = "extra-menu-173";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_173].m_Function = [&]() {
        return m_aExtraMenuActive[172];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_174].m_pId = "extra-menu-174";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_174].m_Function = [&]() {
        return m_aExtraMenuActive[173];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_175].m_pId = "extra-menu-175";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_175].m_Function = [&]() {
        return m_aExtraMenuActive[174];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_176].m_pId = "extra-menu-176";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_176].m_Function = [&]() {
        return m_aExtraMenuActive[175];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_177].m_pId = "extra-menu-177";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_177].m_Function = [&]() {
        return m_aExtraMenuActive[176];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_178].m_pId = "extra-menu-178";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_178].m_Function = [&]() {
        return m_aExtraMenuActive[177];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_179].m_pId = "extra-menu-179";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_179].m_Function = [&]() {
        return m_aExtraMenuActive[178];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_180].m_pId = "extra-menu-180";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_180].m_Function = [&]() {
        return m_aExtraMenuActive[179];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_181].m_pId = "extra-menu-181";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_181].m_Function = [&]() {
        return m_aExtraMenuActive[180];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_182].m_pId = "extra-menu-182";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_182].m_Function = [&]() {
        return m_aExtraMenuActive[181];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_183].m_pId = "extra-menu-183";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_183].m_Function = [&]() {
        return m_aExtraMenuActive[182];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_184].m_pId = "extra-menu-184";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_184].m_Function = [&]() {
        return m_aExtraMenuActive[183];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_185].m_pId = "extra-menu-185";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_185].m_Function = [&]() {
        return m_aExtraMenuActive[184];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_186].m_pId = "extra-menu-186";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_186].m_Function = [&]() {
        return m_aExtraMenuActive[185];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_187].m_pId = "extra-menu-187";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_187].m_Function = [&]() {
        return m_aExtraMenuActive[186];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_188].m_pId = "extra-menu-188";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_188].m_Function = [&]() {
        return m_aExtraMenuActive[187];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_189].m_pId = "extra-menu-189";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_189].m_Function = [&]() {
        return m_aExtraMenuActive[188];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_190].m_pId = "extra-menu-190";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_190].m_Function = [&]() {
        return m_aExtraMenuActive[189];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_191].m_pId = "extra-menu-191";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_191].m_Function = [&]() {
        return m_aExtraMenuActive[190];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_192].m_pId = "extra-menu-192";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_192].m_Function = [&]() {
        return m_aExtraMenuActive[191];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_193].m_pId = "extra-menu-193";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_193].m_Function = [&]() {
        return m_aExtraMenuActive[192];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_194].m_pId = "extra-menu-194";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_194].m_Function = [&]() {
        return m_aExtraMenuActive[193];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_195].m_pId = "extra-menu-195";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_195].m_Function = [&]() {
        return m_aExtraMenuActive[194];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_196].m_pId = "extra-menu-196";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_196].m_Function = [&]() {
        return m_aExtraMenuActive[195];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_197].m_pId = "extra-menu-197";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_197].m_Function = [&]() {
        return m_aExtraMenuActive[196];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_198].m_pId = "extra-menu-198";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_198].m_Function = [&]() {
        return m_aExtraMenuActive[197];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_199].m_pId = "extra-menu-199";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_199].m_Function = [&]() {
        return m_aExtraMenuActive[198];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_200].m_pId = "extra-menu-200";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_200].m_Function = [&]() {
        return m_aExtraMenuActive[199];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_201].m_pId = "extra-menu-201";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_201].m_Function = [&]() {
        return m_aExtraMenuActive[200];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_202].m_pId = "extra-menu-202";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_202].m_Function = [&]() {
        return m_aExtraMenuActive[201];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_203].m_pId = "extra-menu-203";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_203].m_Function = [&]() {
        return m_aExtraMenuActive[202];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_204].m_pId = "extra-menu-204";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_204].m_Function = [&]() {
        return m_aExtraMenuActive[203];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_205].m_pId = "extra-menu-205";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_205].m_Function = [&]() {
        return m_aExtraMenuActive[204];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_206].m_pId = "extra-menu-206";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_206].m_Function = [&]() {
        return m_aExtraMenuActive[205];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_207].m_pId = "extra-menu-207";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_207].m_Function = [&]() {
        return m_aExtraMenuActive[206];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_208].m_pId = "extra-menu-208";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_208].m_Function = [&]() {
        return m_aExtraMenuActive[207];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_209].m_pId = "extra-menu-209";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_209].m_Function = [&]() {
        return m_aExtraMenuActive[208];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_210].m_pId = "extra-menu-210";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_210].m_Function = [&]() {
        return m_aExtraMenuActive[209];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_211].m_pId = "extra-menu-211";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_211].m_Function = [&]() {
        return m_aExtraMenuActive[210];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_212].m_pId = "extra-menu-212";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_212].m_Function = [&]() {
        return m_aExtraMenuActive[211];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_213].m_pId = "extra-menu-213";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_213].m_Function = [&]() {
        return m_aExtraMenuActive[212];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_214].m_pId = "extra-menu-214";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_214].m_Function = [&]() {
        return m_aExtraMenuActive[213];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_215].m_pId = "extra-menu-215";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_215].m_Function = [&]() {
        return m_aExtraMenuActive[214];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_216].m_pId = "extra-menu-216";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_216].m_Function = [&]() {
        return m_aExtraMenuActive[215];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_217].m_pId = "extra-menu-217";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_217].m_Function = [&]() {
        return m_aExtraMenuActive[216];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_218].m_pId = "extra-menu-218";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_218].m_Function = [&]() {
        return m_aExtraMenuActive[217];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_219].m_pId = "extra-menu-219";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_219].m_Function = [&]() {
        return m_aExtraMenuActive[218];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_220].m_pId = "extra-menu-220";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_220].m_Function = [&]() {
        return m_aExtraMenuActive[219];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_221].m_pId = "extra-menu-221";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_221].m_Function = [&]() {
        return m_aExtraMenuActive[220];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_222].m_pId = "extra-menu-222";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_222].m_Function = [&]() {
        return m_aExtraMenuActive[221];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_223].m_pId = "extra-menu-223";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_223].m_Function = [&]() {
        return m_aExtraMenuActive[222];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_224].m_pId = "extra-menu-224";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_224].m_Function = [&]() {
        return m_aExtraMenuActive[223];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_225].m_pId = "extra-menu-225";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_225].m_Function = [&]() {
        return m_aExtraMenuActive[224];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_226].m_pId = "extra-menu-226";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_226].m_Function = [&]() {
        return m_aExtraMenuActive[225];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_227].m_pId = "extra-menu-227";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_227].m_Function = [&]() {
        return m_aExtraMenuActive[226];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_228].m_pId = "extra-menu-228";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_228].m_Function = [&]() {
        return m_aExtraMenuActive[227];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_229].m_pId = "extra-menu-229";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_229].m_Function = [&]() {
        return m_aExtraMenuActive[228];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_230].m_pId = "extra-menu-230";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_230].m_Function = [&]() {
        return m_aExtraMenuActive[229];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_231].m_pId = "extra-menu-231";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_231].m_Function = [&]() {
        return m_aExtraMenuActive[230];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_232].m_pId = "extra-menu-232";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_232].m_Function = [&]() {
        return m_aExtraMenuActive[231];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_233].m_pId = "extra-menu-233";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_233].m_Function = [&]() {
        return m_aExtraMenuActive[232];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_234].m_pId = "extra-menu-234";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_234].m_Function = [&]() {
        return m_aExtraMenuActive[233];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_235].m_pId = "extra-menu-235";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_235].m_Function = [&]() {
        return m_aExtraMenuActive[234];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_236].m_pId = "extra-menu-236";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_236].m_Function = [&]() {
        return m_aExtraMenuActive[235];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_237].m_pId = "extra-menu-237";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_237].m_Function = [&]() {
        return m_aExtraMenuActive[236];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_238].m_pId = "extra-menu-238";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_238].m_Function = [&]() {
        return m_aExtraMenuActive[237];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_239].m_pId = "extra-menu-239";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_239].m_Function = [&]() {
        return m_aExtraMenuActive[238];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_240].m_pId = "extra-menu-240";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_240].m_Function = [&]() {
        return m_aExtraMenuActive[239];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_241].m_pId = "extra-menu-241";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_241].m_Function = [&]() {
        return m_aExtraMenuActive[240];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_242].m_pId = "extra-menu-242";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_242].m_Function = [&]() {
        return m_aExtraMenuActive[241];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_243].m_pId = "extra-menu-243";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_243].m_Function = [&]() {
        return m_aExtraMenuActive[242];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_244].m_pId = "extra-menu-244";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_244].m_Function = [&]() {
        return m_aExtraMenuActive[243];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_245].m_pId = "extra-menu-245";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_245].m_Function = [&]() {
        return m_aExtraMenuActive[244];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_246].m_pId = "extra-menu-246";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_246].m_Function = [&]() {
        return m_aExtraMenuActive[245];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_247].m_pId = "extra-menu-247";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_247].m_Function = [&]() {
        return m_aExtraMenuActive[246];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_248].m_pId = "extra-menu-248";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_248].m_Function = [&]() {
        return m_aExtraMenuActive[247];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_249].m_pId = "extra-menu-249";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_249].m_Function = [&]() {
        return m_aExtraMenuActive[248];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_250].m_pId = "extra-menu-250";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_250].m_Function = [&]() {
        return m_aExtraMenuActive[249];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_251].m_pId = "extra-menu-251";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_251].m_Function = [&]() {
        return m_aExtraMenuActive[250];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_252].m_pId = "extra-menu-252";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_252].m_Function = [&]() {
        return m_aExtraMenuActive[251];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_253].m_pId = "extra-menu-253";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_253].m_Function = [&]() {
        return m_aExtraMenuActive[252];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_254].m_pId = "extra-menu-254";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_254].m_Function = [&]() {
        return m_aExtraMenuActive[253];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_255].m_pId = "extra-menu-255";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_255].m_Function = [&]() {
        return m_aExtraMenuActive[254];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_256].m_pId = "extra-menu-256";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_256].m_Function = [&]() {
        return m_aExtraMenuActive[255];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_257].m_pId = "extra-menu-257";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_257].m_Function = [&]() {
        return m_aExtraMenuActive[256];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_258].m_pId = "extra-menu-258";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_258].m_Function = [&]() {
        return m_aExtraMenuActive[257];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_259].m_pId = "extra-menu-259";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_259].m_Function = [&]() {
        return m_aExtraMenuActive[258];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_260].m_pId = "extra-menu-260";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_260].m_Function = [&]() {
        return m_aExtraMenuActive[259];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_261].m_pId = "extra-menu-261";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_261].m_Function = [&]() {
        return m_aExtraMenuActive[260];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_262].m_pId = "extra-menu-262";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_262].m_Function = [&]() {
        return m_aExtraMenuActive[261];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_263].m_pId = "extra-menu-263";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_263].m_Function = [&]() {
        return m_aExtraMenuActive[262];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_264].m_pId = "extra-menu-264";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_264].m_Function = [&]() {
        return m_aExtraMenuActive[263];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_265].m_pId = "extra-menu-265";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_265].m_Function = [&]() {
        return m_aExtraMenuActive[264];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_266].m_pId = "extra-menu-266";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_266].m_Function = [&]() {
        return m_aExtraMenuActive[265];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_267].m_pId = "extra-menu-267";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_267].m_Function = [&]() {
        return m_aExtraMenuActive[266];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_268].m_pId = "extra-menu-268";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_268].m_Function = [&]() {
        return m_aExtraMenuActive[267];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_269].m_pId = "extra-menu-269";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_269].m_Function = [&]() {
        return m_aExtraMenuActive[268];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_270].m_pId = "extra-menu-270";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_270].m_Function = [&]() {
        return m_aExtraMenuActive[269];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_271].m_pId = "extra-menu-271";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_271].m_Function = [&]() {
        return m_aExtraMenuActive[270];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_272].m_pId = "extra-menu-272";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_272].m_Function = [&]() {
        return m_aExtraMenuActive[271];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_273].m_pId = "extra-menu-273";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_273].m_Function = [&]() {
        return m_aExtraMenuActive[272];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_274].m_pId = "extra-menu-274";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_274].m_Function = [&]() {
        return m_aExtraMenuActive[273];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_275].m_pId = "extra-menu-275";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_275].m_Function = [&]() {
        return m_aExtraMenuActive[274];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_276].m_pId = "extra-menu-276";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_276].m_Function = [&]() {
        return m_aExtraMenuActive[275];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_277].m_pId = "extra-menu-277";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_277].m_Function = [&]() {
        return m_aExtraMenuActive[276];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_278].m_pId = "extra-menu-278";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_278].m_Function = [&]() {
        return m_aExtraMenuActive[277];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_279].m_pId = "extra-menu-279";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_279].m_Function = [&]() {
        return m_aExtraMenuActive[278];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_280].m_pId = "extra-menu-280";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_280].m_Function = [&]() {
        return m_aExtraMenuActive[279];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_281].m_pId = "extra-menu-281";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_281].m_Function = [&]() {
        return m_aExtraMenuActive[280];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_282].m_pId = "extra-menu-282";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_282].m_Function = [&]() {
        return m_aExtraMenuActive[281];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_283].m_pId = "extra-menu-283";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_283].m_Function = [&]() {
        return m_aExtraMenuActive[282];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_284].m_pId = "extra-menu-284";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_284].m_Function = [&]() {
        return m_aExtraMenuActive[283];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_285].m_pId = "extra-menu-285";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_285].m_Function = [&]() {
        return m_aExtraMenuActive[284];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_286].m_pId = "extra-menu-286";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_286].m_Function = [&]() {
        return m_aExtraMenuActive[285];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_287].m_pId = "extra-menu-287";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_287].m_Function = [&]() {
        return m_aExtraMenuActive[286];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_288].m_pId = "extra-menu-288";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_288].m_Function = [&]() {
        return m_aExtraMenuActive[287];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_289].m_pId = "extra-menu-289";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_289].m_Function = [&]() {
        return m_aExtraMenuActive[288];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_290].m_pId = "extra-menu-290";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_290].m_Function = [&]() {
        return m_aExtraMenuActive[289];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_291].m_pId = "extra-menu-291";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_291].m_Function = [&]() {
        return m_aExtraMenuActive[290];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_292].m_pId = "extra-menu-292";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_292].m_Function = [&]() {
        return m_aExtraMenuActive[291];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_293].m_pId = "extra-menu-293";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_293].m_Function = [&]() {
        return m_aExtraMenuActive[292];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_294].m_pId = "extra-menu-294";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_294].m_Function = [&]() {
        return m_aExtraMenuActive[293];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_295].m_pId = "extra-menu-295";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_295].m_Function = [&]() {
        return m_aExtraMenuActive[294];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_296].m_pId = "extra-menu-296";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_296].m_Function = [&]() {
        return m_aExtraMenuActive[295];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_297].m_pId = "extra-menu-297";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_297].m_Function = [&]() {
        return m_aExtraMenuActive[296];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_298].m_pId = "extra-menu-298";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_298].m_Function = [&]() {
        return m_aExtraMenuActive[297];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_299].m_pId = "extra-menu-299";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_299].m_Function = [&]() {
        return m_aExtraMenuActive[298];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_300].m_pId = "extra-menu-300";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_300].m_Function = [&]() {
        return m_aExtraMenuActive[299];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_301].m_pId = "extra-menu-301";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_301].m_Function = [&]() {
        return m_aExtraMenuActive[300];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_302].m_pId = "extra-menu-302";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_302].m_Function = [&]() {
        return m_aExtraMenuActive[301];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_303].m_pId = "extra-menu-303";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_303].m_Function = [&]() {
        return m_aExtraMenuActive[302];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_304].m_pId = "extra-menu-304";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_304].m_Function = [&]() {
        return m_aExtraMenuActive[303];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_305].m_pId = "extra-menu-305";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_305].m_Function = [&]() {
        return m_aExtraMenuActive[304];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_306].m_pId = "extra-menu-306";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_306].m_Function = [&]() {
        return m_aExtraMenuActive[305];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_307].m_pId = "extra-menu-307";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_307].m_Function = [&]() {
        return m_aExtraMenuActive[306];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_308].m_pId = "extra-menu-308";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_308].m_Function = [&]() {
        return m_aExtraMenuActive[307];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_309].m_pId = "extra-menu-309";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_309].m_Function = [&]() {
        return m_aExtraMenuActive[308];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_310].m_pId = "extra-menu-310";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_310].m_Function = [&]() {
        return m_aExtraMenuActive[309];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_311].m_pId = "extra-menu-311";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_311].m_Function = [&]() {
        return m_aExtraMenuActive[310];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_312].m_pId = "extra-menu-312";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_312].m_Function = [&]() {
        return m_aExtraMenuActive[311];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_313].m_pId = "extra-menu-313";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_313].m_Function = [&]() {
        return m_aExtraMenuActive[312];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_314].m_pId = "extra-menu-314";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_314].m_Function = [&]() {
        return m_aExtraMenuActive[313];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_315].m_pId = "extra-menu-315";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_315].m_Function = [&]() {
        return m_aExtraMenuActive[314];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_316].m_pId = "extra-menu-316";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_316].m_Function = [&]() {
        return m_aExtraMenuActive[315];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_317].m_pId = "extra-menu-317";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_317].m_Function = [&]() {
        return m_aExtraMenuActive[316];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_318].m_pId = "extra-menu-318";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_318].m_Function = [&]() {
        return m_aExtraMenuActive[317];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_319].m_pId = "extra-menu-319";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_319].m_Function = [&]() {
        return m_aExtraMenuActive[318];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_320].m_pId = "extra-menu-320";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_320].m_Function = [&]() {
        return m_aExtraMenuActive[319];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_321].m_pId = "extra-menu-321";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_321].m_Function = [&]() {
        return m_aExtraMenuActive[320];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_322].m_pId = "extra-menu-322";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_322].m_Function = [&]() {
        return m_aExtraMenuActive[321];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_323].m_pId = "extra-menu-323";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_323].m_Function = [&]() {
        return m_aExtraMenuActive[322];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_324].m_pId = "extra-menu-324";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_324].m_Function = [&]() {
        return m_aExtraMenuActive[323];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_325].m_pId = "extra-menu-325";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_325].m_Function = [&]() {
        return m_aExtraMenuActive[324];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_326].m_pId = "extra-menu-326";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_326].m_Function = [&]() {
        return m_aExtraMenuActive[325];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_327].m_pId = "extra-menu-327";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_327].m_Function = [&]() {
        return m_aExtraMenuActive[326];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_328].m_pId = "extra-menu-328";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_328].m_Function = [&]() {
        return m_aExtraMenuActive[327];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_329].m_pId = "extra-menu-329";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_329].m_Function = [&]() {
        return m_aExtraMenuActive[328];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_330].m_pId = "extra-menu-330";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_330].m_Function = [&]() {
        return m_aExtraMenuActive[329];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_331].m_pId = "extra-menu-331";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_331].m_Function = [&]() {
        return m_aExtraMenuActive[330];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_332].m_pId = "extra-menu-332";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_332].m_Function = [&]() {
        return m_aExtraMenuActive[331];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_333].m_pId = "extra-menu-333";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_333].m_Function = [&]() {
        return m_aExtraMenuActive[332];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_334].m_pId = "extra-menu-334";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_334].m_Function = [&]() {
        return m_aExtraMenuActive[333];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_335].m_pId = "extra-menu-335";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_335].m_Function = [&]() {
        return m_aExtraMenuActive[334];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_336].m_pId = "extra-menu-336";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_336].m_Function = [&]() {
        return m_aExtraMenuActive[335];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_337].m_pId = "extra-menu-337";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_337].m_Function = [&]() {
        return m_aExtraMenuActive[336];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_338].m_pId = "extra-menu-338";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_338].m_Function = [&]() {
        return m_aExtraMenuActive[337];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_339].m_pId = "extra-menu-339";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_339].m_Function = [&]() {
        return m_aExtraMenuActive[338];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_340].m_pId = "extra-menu-340";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_340].m_Function = [&]() {
        return m_aExtraMenuActive[339];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_341].m_pId = "extra-menu-341";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_341].m_Function = [&]() {
        return m_aExtraMenuActive[340];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_342].m_pId = "extra-menu-342";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_342].m_Function = [&]() {
        return m_aExtraMenuActive[341];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_343].m_pId = "extra-menu-343";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_343].m_Function = [&]() {
        return m_aExtraMenuActive[342];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_344].m_pId = "extra-menu-344";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_344].m_Function = [&]() {
        return m_aExtraMenuActive[343];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_345].m_pId = "extra-menu-345";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_345].m_Function = [&]() {
        return m_aExtraMenuActive[344];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_346].m_pId = "extra-menu-346";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_346].m_Function = [&]() {
        return m_aExtraMenuActive[345];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_347].m_pId = "extra-menu-347";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_347].m_Function = [&]() {
        return m_aExtraMenuActive[346];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_348].m_pId = "extra-menu-348";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_348].m_Function = [&]() {
        return m_aExtraMenuActive[347];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_349].m_pId = "extra-menu-349";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_349].m_Function = [&]() {
        return m_aExtraMenuActive[348];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_350].m_pId = "extra-menu-350";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_350].m_Function = [&]() {
        return m_aExtraMenuActive[349];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_351].m_pId = "extra-menu-351";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_351].m_Function = [&]() {
        return m_aExtraMenuActive[350];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_352].m_pId = "extra-menu-352";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_352].m_Function = [&]() {
        return m_aExtraMenuActive[351];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_353].m_pId = "extra-menu-353";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_353].m_Function = [&]() {
        return m_aExtraMenuActive[352];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_354].m_pId = "extra-menu-354";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_354].m_Function = [&]() {
        return m_aExtraMenuActive[353];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_355].m_pId = "extra-menu-355";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_355].m_Function = [&]() {
        return m_aExtraMenuActive[354];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_356].m_pId = "extra-menu-356";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_356].m_Function = [&]() {
        return m_aExtraMenuActive[355];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_357].m_pId = "extra-menu-357";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_357].m_Function = [&]() {
        return m_aExtraMenuActive[356];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_358].m_pId = "extra-menu-358";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_358].m_Function = [&]() {
        return m_aExtraMenuActive[357];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_359].m_pId = "extra-menu-359";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_359].m_Function = [&]() {
        return m_aExtraMenuActive[358];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_360].m_pId = "extra-menu-360";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_360].m_Function = [&]() {
        return m_aExtraMenuActive[359];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_361].m_pId = "extra-menu-361";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_361].m_Function = [&]() {
        return m_aExtraMenuActive[360];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_362].m_pId = "extra-menu-362";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_362].m_Function = [&]() {
        return m_aExtraMenuActive[361];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_363].m_pId = "extra-menu-363";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_363].m_Function = [&]() {
        return m_aExtraMenuActive[362];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_364].m_pId = "extra-menu-364";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_364].m_Function = [&]() {
        return m_aExtraMenuActive[363];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_365].m_pId = "extra-menu-365";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_365].m_Function = [&]() {
        return m_aExtraMenuActive[364];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_366].m_pId = "extra-menu-366";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_366].m_Function = [&]() {
        return m_aExtraMenuActive[365];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_367].m_pId = "extra-menu-367";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_367].m_Function = [&]() {
        return m_aExtraMenuActive[366];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_368].m_pId = "extra-menu-368";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_368].m_Function = [&]() {
        return m_aExtraMenuActive[367];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_369].m_pId = "extra-menu-369";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_369].m_Function = [&]() {
        return m_aExtraMenuActive[368];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_370].m_pId = "extra-menu-370";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_370].m_Function = [&]() {
        return m_aExtraMenuActive[369];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_371].m_pId = "extra-menu-371";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_371].m_Function = [&]() {
        return m_aExtraMenuActive[370];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_372].m_pId = "extra-menu-372";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_372].m_Function = [&]() {
        return m_aExtraMenuActive[371];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_373].m_pId = "extra-menu-373";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_373].m_Function = [&]() {
        return m_aExtraMenuActive[372];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_374].m_pId = "extra-menu-374";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_374].m_Function = [&]() {
        return m_aExtraMenuActive[373];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_375].m_pId = "extra-menu-375";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_375].m_Function = [&]() {
        return m_aExtraMenuActive[374];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_376].m_pId = "extra-menu-376";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_376].m_Function = [&]() {
        return m_aExtraMenuActive[375];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_377].m_pId = "extra-menu-377";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_377].m_Function = [&]() {
        return m_aExtraMenuActive[376];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_378].m_pId = "extra-menu-378";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_378].m_Function = [&]() {
        return m_aExtraMenuActive[377];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_379].m_pId = "extra-menu-379";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_379].m_Function = [&]() {
        return m_aExtraMenuActive[378];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_380].m_pId = "extra-menu-380";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_380].m_Function = [&]() {
        return m_aExtraMenuActive[379];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_381].m_pId = "extra-menu-381";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_381].m_Function = [&]() {
        return m_aExtraMenuActive[380];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_382].m_pId = "extra-menu-382";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_382].m_Function = [&]() {
        return m_aExtraMenuActive[381];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_383].m_pId = "extra-menu-383";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_383].m_Function = [&]() {
        return m_aExtraMenuActive[382];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_384].m_pId = "extra-menu-384";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_384].m_Function = [&]() {
        return m_aExtraMenuActive[383];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_385].m_pId = "extra-menu-385";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_385].m_Function = [&]() {
        return m_aExtraMenuActive[384];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_386].m_pId = "extra-menu-386";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_386].m_Function = [&]() {
        return m_aExtraMenuActive[385];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_387].m_pId = "extra-menu-387";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_387].m_Function = [&]() {
        return m_aExtraMenuActive[386];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_388].m_pId = "extra-menu-388";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_388].m_Function = [&]() {
        return m_aExtraMenuActive[387];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_389].m_pId = "extra-menu-389";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_389].m_Function = [&]() {
        return m_aExtraMenuActive[388];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_390].m_pId = "extra-menu-390";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_390].m_Function = [&]() {
        return m_aExtraMenuActive[389];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_391].m_pId = "extra-menu-391";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_391].m_Function = [&]() {
        return m_aExtraMenuActive[390];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_392].m_pId = "extra-menu-392";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_392].m_Function = [&]() {
        return m_aExtraMenuActive[391];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_393].m_pId = "extra-menu-393";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_393].m_Function = [&]() {
        return m_aExtraMenuActive[392];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_394].m_pId = "extra-menu-394";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_394].m_Function = [&]() {
        return m_aExtraMenuActive[393];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_395].m_pId = "extra-menu-395";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_395].m_Function = [&]() {
        return m_aExtraMenuActive[394];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_396].m_pId = "extra-menu-396";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_396].m_Function = [&]() {
        return m_aExtraMenuActive[395];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_397].m_pId = "extra-menu-397";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_397].m_Function = [&]() {
        return m_aExtraMenuActive[396];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_398].m_pId = "extra-menu-398";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_398].m_Function = [&]() {
        return m_aExtraMenuActive[397];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_399].m_pId = "extra-menu-399";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_399].m_Function = [&]() {
        return m_aExtraMenuActive[398];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_400].m_pId = "extra-menu-400";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_400].m_Function = [&]() {
        return m_aExtraMenuActive[399];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_401].m_pId = "extra-menu-401";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_401].m_Function = [&]() {
        return m_aExtraMenuActive[400];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_402].m_pId = "extra-menu-402";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_402].m_Function = [&]() {
        return m_aExtraMenuActive[401];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_403].m_pId = "extra-menu-403";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_403].m_Function = [&]() {
        return m_aExtraMenuActive[402];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_404].m_pId = "extra-menu-404";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_404].m_Function = [&]() {
        return m_aExtraMenuActive[403];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_405].m_pId = "extra-menu-405";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_405].m_Function = [&]() {
        return m_aExtraMenuActive[404];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_406].m_pId = "extra-menu-406";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_406].m_Function = [&]() {
        return m_aExtraMenuActive[405];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_407].m_pId = "extra-menu-407";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_407].m_Function = [&]() {
        return m_aExtraMenuActive[406];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_408].m_pId = "extra-menu-408";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_408].m_Function = [&]() {
        return m_aExtraMenuActive[407];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_409].m_pId = "extra-menu-409";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_409].m_Function = [&]() {
        return m_aExtraMenuActive[408];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_410].m_pId = "extra-menu-410";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_410].m_Function = [&]() {
        return m_aExtraMenuActive[409];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_411].m_pId = "extra-menu-411";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_411].m_Function = [&]() {
        return m_aExtraMenuActive[410];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_412].m_pId = "extra-menu-412";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_412].m_Function = [&]() {
        return m_aExtraMenuActive[411];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_413].m_pId = "extra-menu-413";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_413].m_Function = [&]() {
        return m_aExtraMenuActive[412];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_414].m_pId = "extra-menu-414";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_414].m_Function = [&]() {
        return m_aExtraMenuActive[413];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_415].m_pId = "extra-menu-415";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_415].m_Function = [&]() {
        return m_aExtraMenuActive[414];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_416].m_pId = "extra-menu-416";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_416].m_Function = [&]() {
        return m_aExtraMenuActive[415];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_417].m_pId = "extra-menu-417";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_417].m_Function = [&]() {
        return m_aExtraMenuActive[416];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_418].m_pId = "extra-menu-418";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_418].m_Function = [&]() {
        return m_aExtraMenuActive[417];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_419].m_pId = "extra-menu-419";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_419].m_Function = [&]() {
        return m_aExtraMenuActive[418];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_420].m_pId = "extra-menu-420";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_420].m_Function = [&]() {
        return m_aExtraMenuActive[419];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_421].m_pId = "extra-menu-421";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_421].m_Function = [&]() {
        return m_aExtraMenuActive[420];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_422].m_pId = "extra-menu-422";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_422].m_Function = [&]() {
        return m_aExtraMenuActive[421];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_423].m_pId = "extra-menu-423";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_423].m_Function = [&]() {
        return m_aExtraMenuActive[422];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_424].m_pId = "extra-menu-424";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_424].m_Function = [&]() {
        return m_aExtraMenuActive[423];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_425].m_pId = "extra-menu-425";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_425].m_Function = [&]() {
        return m_aExtraMenuActive[424];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_426].m_pId = "extra-menu-426";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_426].m_Function = [&]() {
        return m_aExtraMenuActive[425];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_427].m_pId = "extra-menu-427";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_427].m_Function = [&]() {
        return m_aExtraMenuActive[426];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_428].m_pId = "extra-menu-428";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_428].m_Function = [&]() {
        return m_aExtraMenuActive[427];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_429].m_pId = "extra-menu-429";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_429].m_Function = [&]() {
        return m_aExtraMenuActive[428];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_430].m_pId = "extra-menu-430";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_430].m_Function = [&]() {
        return m_aExtraMenuActive[429];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_431].m_pId = "extra-menu-431";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_431].m_Function = [&]() {
        return m_aExtraMenuActive[430];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_432].m_pId = "extra-menu-432";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_432].m_Function = [&]() {
        return m_aExtraMenuActive[431];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_433].m_pId = "extra-menu-433";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_433].m_Function = [&]() {
        return m_aExtraMenuActive[432];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_434].m_pId = "extra-menu-434";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_434].m_Function = [&]() {
        return m_aExtraMenuActive[433];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_435].m_pId = "extra-menu-435";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_435].m_Function = [&]() {
        return m_aExtraMenuActive[434];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_436].m_pId = "extra-menu-436";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_436].m_Function = [&]() {
        return m_aExtraMenuActive[435];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_437].m_pId = "extra-menu-437";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_437].m_Function = [&]() {
        return m_aExtraMenuActive[436];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_438].m_pId = "extra-menu-438";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_438].m_Function = [&]() {
        return m_aExtraMenuActive[437];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_439].m_pId = "extra-menu-439";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_439].m_Function = [&]() {
        return m_aExtraMenuActive[438];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_440].m_pId = "extra-menu-440";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_440].m_Function = [&]() {
        return m_aExtraMenuActive[439];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_441].m_pId = "extra-menu-441";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_441].m_Function = [&]() {
        return m_aExtraMenuActive[440];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_442].m_pId = "extra-menu-442";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_442].m_Function = [&]() {
        return m_aExtraMenuActive[441];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_443].m_pId = "extra-menu-443";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_443].m_Function = [&]() {
        return m_aExtraMenuActive[442];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_444].m_pId = "extra-menu-444";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_444].m_Function = [&]() {
        return m_aExtraMenuActive[443];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_445].m_pId = "extra-menu-445";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_445].m_Function = [&]() {
        return m_aExtraMenuActive[444];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_446].m_pId = "extra-menu-446";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_446].m_Function = [&]() {
        return m_aExtraMenuActive[445];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_447].m_pId = "extra-menu-447";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_447].m_Function = [&]() {
        return m_aExtraMenuActive[446];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_448].m_pId = "extra-menu-448";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_448].m_Function = [&]() {
        return m_aExtraMenuActive[447];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_449].m_pId = "extra-menu-449";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_449].m_Function = [&]() {
        return m_aExtraMenuActive[448];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_450].m_pId = "extra-menu-450";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_450].m_Function = [&]() {
        return m_aExtraMenuActive[449];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_451].m_pId = "extra-menu-451";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_451].m_Function = [&]() {
        return m_aExtraMenuActive[450];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_452].m_pId = "extra-menu-452";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_452].m_Function = [&]() {
        return m_aExtraMenuActive[451];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_453].m_pId = "extra-menu-453";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_453].m_Function = [&]() {
        return m_aExtraMenuActive[452];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_454].m_pId = "extra-menu-454";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_454].m_Function = [&]() {
        return m_aExtraMenuActive[453];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_455].m_pId = "extra-menu-455";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_455].m_Function = [&]() {
        return m_aExtraMenuActive[454];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_456].m_pId = "extra-menu-456";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_456].m_Function = [&]() {
        return m_aExtraMenuActive[455];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_457].m_pId = "extra-menu-457";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_457].m_Function = [&]() {
        return m_aExtraMenuActive[456];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_458].m_pId = "extra-menu-458";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_458].m_Function = [&]() {
        return m_aExtraMenuActive[457];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_459].m_pId = "extra-menu-459";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_459].m_Function = [&]() {
        return m_aExtraMenuActive[458];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_460].m_pId = "extra-menu-460";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_460].m_Function = [&]() {
        return m_aExtraMenuActive[459];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_461].m_pId = "extra-menu-461";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_461].m_Function = [&]() {
        return m_aExtraMenuActive[460];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_462].m_pId = "extra-menu-462";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_462].m_Function = [&]() {
        return m_aExtraMenuActive[461];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_463].m_pId = "extra-menu-463";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_463].m_Function = [&]() {
        return m_aExtraMenuActive[462];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_464].m_pId = "extra-menu-464";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_464].m_Function = [&]() {
        return m_aExtraMenuActive[463];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_465].m_pId = "extra-menu-465";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_465].m_Function = [&]() {
        return m_aExtraMenuActive[464];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_466].m_pId = "extra-menu-466";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_466].m_Function = [&]() {
        return m_aExtraMenuActive[465];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_467].m_pId = "extra-menu-467";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_467].m_Function = [&]() {
        return m_aExtraMenuActive[466];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_468].m_pId = "extra-menu-468";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_468].m_Function = [&]() {
        return m_aExtraMenuActive[467];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_469].m_pId = "extra-menu-469";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_469].m_Function = [&]() {
        return m_aExtraMenuActive[468];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_470].m_pId = "extra-menu-470";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_470].m_Function = [&]() {
        return m_aExtraMenuActive[469];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_471].m_pId = "extra-menu-471";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_471].m_Function = [&]() {
        return m_aExtraMenuActive[470];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_472].m_pId = "extra-menu-472";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_472].m_Function = [&]() {
        return m_aExtraMenuActive[471];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_473].m_pId = "extra-menu-473";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_473].m_Function = [&]() {
        return m_aExtraMenuActive[472];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_474].m_pId = "extra-menu-474";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_474].m_Function = [&]() {
        return m_aExtraMenuActive[473];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_475].m_pId = "extra-menu-475";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_475].m_Function = [&]() {
        return m_aExtraMenuActive[474];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_476].m_pId = "extra-menu-476";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_476].m_Function = [&]() {
        return m_aExtraMenuActive[475];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_477].m_pId = "extra-menu-477";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_477].m_Function = [&]() {
        return m_aExtraMenuActive[476];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_478].m_pId = "extra-menu-478";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_478].m_Function = [&]() {
        return m_aExtraMenuActive[477];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_479].m_pId = "extra-menu-479";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_479].m_Function = [&]() {
        return m_aExtraMenuActive[478];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_480].m_pId = "extra-menu-480";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_480].m_Function = [&]() {
        return m_aExtraMenuActive[479];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_481].m_pId = "extra-menu-481";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_481].m_Function = [&]() {
        return m_aExtraMenuActive[480];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_482].m_pId = "extra-menu-482";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_482].m_Function = [&]() {
        return m_aExtraMenuActive[481];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_483].m_pId = "extra-menu-483";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_483].m_Function = [&]() {
        return m_aExtraMenuActive[482];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_484].m_pId = "extra-menu-484";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_484].m_Function = [&]() {
        return m_aExtraMenuActive[483];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_485].m_pId = "extra-menu-485";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_485].m_Function = [&]() {
        return m_aExtraMenuActive[484];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_486].m_pId = "extra-menu-486";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_486].m_Function = [&]() {
        return m_aExtraMenuActive[485];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_487].m_pId = "extra-menu-487";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_487].m_Function = [&]() {
        return m_aExtraMenuActive[486];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_488].m_pId = "extra-menu-488";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_488].m_Function = [&]() {
        return m_aExtraMenuActive[487];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_489].m_pId = "extra-menu-489";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_489].m_Function = [&]() {
        return m_aExtraMenuActive[488];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_490].m_pId = "extra-menu-490";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_490].m_Function = [&]() {
        return m_aExtraMenuActive[489];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_491].m_pId = "extra-menu-491";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_491].m_Function = [&]() {
        return m_aExtraMenuActive[490];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_492].m_pId = "extra-menu-492";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_492].m_Function = [&]() {
        return m_aExtraMenuActive[491];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_493].m_pId = "extra-menu-493";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_493].m_Function = [&]() {
        return m_aExtraMenuActive[492];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_494].m_pId = "extra-menu-494";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_494].m_Function = [&]() {
        return m_aExtraMenuActive[493];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_495].m_pId = "extra-menu-495";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_495].m_Function = [&]() {
        return m_aExtraMenuActive[494];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_496].m_pId = "extra-menu-496";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_496].m_Function = [&]() {
        return m_aExtraMenuActive[495];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_497].m_pId = "extra-menu-497";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_497].m_Function = [&]() {
        return m_aExtraMenuActive[496];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_498].m_pId = "extra-menu-498";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_498].m_Function = [&]() {
        return m_aExtraMenuActive[497];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_499].m_pId = "extra-menu-499";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_499].m_Function = [&]() {
        return m_aExtraMenuActive[498];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_500].m_pId = "extra-menu-500";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_500].m_Function = [&]() {
        return m_aExtraMenuActive[499];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_501].m_pId = "extra-menu-501";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_501].m_Function = [&]() {
        return m_aExtraMenuActive[500];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_502].m_pId = "extra-menu-502";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_502].m_Function = [&]() {
        return m_aExtraMenuActive[501];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_503].m_pId = "extra-menu-503";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_503].m_Function = [&]() {
        return m_aExtraMenuActive[502];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_504].m_pId = "extra-menu-504";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_504].m_Function = [&]() {
        return m_aExtraMenuActive[503];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_505].m_pId = "extra-menu-505";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_505].m_Function = [&]() {
        return m_aExtraMenuActive[504];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_506].m_pId = "extra-menu-506";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_506].m_Function = [&]() {
        return m_aExtraMenuActive[505];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_507].m_pId = "extra-menu-507";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_507].m_Function = [&]() {
        return m_aExtraMenuActive[506];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_508].m_pId = "extra-menu-508";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_508].m_Function = [&]() {
        return m_aExtraMenuActive[507];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_509].m_pId = "extra-menu-509";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_509].m_Function = [&]() {
        return m_aExtraMenuActive[508];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_510].m_pId = "extra-menu-510";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_510].m_Function = [&]() {
        return m_aExtraMenuActive[509];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_511].m_pId = "extra-menu-511";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_511].m_Function = [&]() {
        return m_aExtraMenuActive[510];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_512].m_pId = "extra-menu-512";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_512].m_Function = [&]() {
        return m_aExtraMenuActive[511];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_513].m_pId = "extra-menu-513";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_513].m_Function = [&]() {
        return m_aExtraMenuActive[512];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_514].m_pId = "extra-menu-514";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_514].m_Function = [&]() {
        return m_aExtraMenuActive[513];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_515].m_pId = "extra-menu-515";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_515].m_Function = [&]() {
        return m_aExtraMenuActive[514];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_516].m_pId = "extra-menu-516";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_516].m_Function = [&]() {
        return m_aExtraMenuActive[515];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_517].m_pId = "extra-menu-517";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_517].m_Function = [&]() {
        return m_aExtraMenuActive[516];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_518].m_pId = "extra-menu-518";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_518].m_Function = [&]() {
        return m_aExtraMenuActive[517];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_519].m_pId = "extra-menu-519";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_519].m_Function = [&]() {
        return m_aExtraMenuActive[518];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_520].m_pId = "extra-menu-520";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_520].m_Function = [&]() {
        return m_aExtraMenuActive[519];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_521].m_pId = "extra-menu-521";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_521].m_Function = [&]() {
        return m_aExtraMenuActive[520];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_522].m_pId = "extra-menu-522";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_522].m_Function = [&]() {
        return m_aExtraMenuActive[521];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_523].m_pId = "extra-menu-523";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_523].m_Function = [&]() {
        return m_aExtraMenuActive[522];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_524].m_pId = "extra-menu-524";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_524].m_Function = [&]() {
        return m_aExtraMenuActive[523];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_525].m_pId = "extra-menu-525";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_525].m_Function = [&]() {
        return m_aExtraMenuActive[524];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_526].m_pId = "extra-menu-526";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_526].m_Function = [&]() {
        return m_aExtraMenuActive[525];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_527].m_pId = "extra-menu-527";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_527].m_Function = [&]() {
        return m_aExtraMenuActive[526];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_528].m_pId = "extra-menu-528";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_528].m_Function = [&]() {
        return m_aExtraMenuActive[527];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_529].m_pId = "extra-menu-529";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_529].m_Function = [&]() {
        return m_aExtraMenuActive[528];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_530].m_pId = "extra-menu-530";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_530].m_Function = [&]() {
        return m_aExtraMenuActive[529];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_531].m_pId = "extra-menu-531";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_531].m_Function = [&]() {
        return m_aExtraMenuActive[530];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_532].m_pId = "extra-menu-532";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_532].m_Function = [&]() {
        return m_aExtraMenuActive[531];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_533].m_pId = "extra-menu-533";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_533].m_Function = [&]() {
        return m_aExtraMenuActive[532];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_534].m_pId = "extra-menu-534";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_534].m_Function = [&]() {
        return m_aExtraMenuActive[533];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_535].m_pId = "extra-menu-535";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_535].m_Function = [&]() {
        return m_aExtraMenuActive[534];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_536].m_pId = "extra-menu-536";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_536].m_Function = [&]() {
        return m_aExtraMenuActive[535];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_537].m_pId = "extra-menu-537";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_537].m_Function = [&]() {
        return m_aExtraMenuActive[536];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_538].m_pId = "extra-menu-538";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_538].m_Function = [&]() {
        return m_aExtraMenuActive[537];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_539].m_pId = "extra-menu-539";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_539].m_Function = [&]() {
        return m_aExtraMenuActive[538];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_540].m_pId = "extra-menu-540";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_540].m_Function = [&]() {
        return m_aExtraMenuActive[539];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_541].m_pId = "extra-menu-541";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_541].m_Function = [&]() {
        return m_aExtraMenuActive[540];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_542].m_pId = "extra-menu-542";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_542].m_Function = [&]() {
        return m_aExtraMenuActive[541];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_543].m_pId = "extra-menu-543";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_543].m_Function = [&]() {
        return m_aExtraMenuActive[542];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_544].m_pId = "extra-menu-544";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_544].m_Function = [&]() {
        return m_aExtraMenuActive[543];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_545].m_pId = "extra-menu-545";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_545].m_Function = [&]() {
        return m_aExtraMenuActive[544];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_546].m_pId = "extra-menu-546";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_546].m_Function = [&]() {
        return m_aExtraMenuActive[545];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_547].m_pId = "extra-menu-547";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_547].m_Function = [&]() {
        return m_aExtraMenuActive[546];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_548].m_pId = "extra-menu-548";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_548].m_Function = [&]() {
        return m_aExtraMenuActive[547];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_549].m_pId = "extra-menu-549";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_549].m_Function = [&]() {
        return m_aExtraMenuActive[548];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_550].m_pId = "extra-menu-550";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_550].m_Function = [&]() {
        return m_aExtraMenuActive[549];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_551].m_pId = "extra-menu-551";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_551].m_Function = [&]() {
        return m_aExtraMenuActive[550];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_552].m_pId = "extra-menu-552";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_552].m_Function = [&]() {
        return m_aExtraMenuActive[551];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_553].m_pId = "extra-menu-553";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_553].m_Function = [&]() {
        return m_aExtraMenuActive[552];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_554].m_pId = "extra-menu-554";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_554].m_Function = [&]() {
        return m_aExtraMenuActive[553];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_555].m_pId = "extra-menu-555";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_555].m_Function = [&]() {
        return m_aExtraMenuActive[554];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_556].m_pId = "extra-menu-556";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_556].m_Function = [&]() {
        return m_aExtraMenuActive[555];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_557].m_pId = "extra-menu-557";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_557].m_Function = [&]() {
        return m_aExtraMenuActive[556];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_558].m_pId = "extra-menu-558";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_558].m_Function = [&]() {
        return m_aExtraMenuActive[557];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_559].m_pId = "extra-menu-559";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_559].m_Function = [&]() {
        return m_aExtraMenuActive[558];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_560].m_pId = "extra-menu-560";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_560].m_Function = [&]() {
        return m_aExtraMenuActive[559];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_561].m_pId = "extra-menu-561";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_561].m_Function = [&]() {
        return m_aExtraMenuActive[560];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_562].m_pId = "extra-menu-562";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_562].m_Function = [&]() {
        return m_aExtraMenuActive[561];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_563].m_pId = "extra-menu-563";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_563].m_Function = [&]() {
        return m_aExtraMenuActive[562];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_564].m_pId = "extra-menu-564";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_564].m_Function = [&]() {
        return m_aExtraMenuActive[563];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_565].m_pId = "extra-menu-565";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_565].m_Function = [&]() {
        return m_aExtraMenuActive[564];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_566].m_pId = "extra-menu-566";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_566].m_Function = [&]() {
        return m_aExtraMenuActive[565];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_567].m_pId = "extra-menu-567";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_567].m_Function = [&]() {
        return m_aExtraMenuActive[566];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_568].m_pId = "extra-menu-568";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_568].m_Function = [&]() {
        return m_aExtraMenuActive[567];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_569].m_pId = "extra-menu-569";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_569].m_Function = [&]() {
        return m_aExtraMenuActive[568];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_570].m_pId = "extra-menu-570";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_570].m_Function = [&]() {
        return m_aExtraMenuActive[569];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_571].m_pId = "extra-menu-571";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_571].m_Function = [&]() {
        return m_aExtraMenuActive[570];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_572].m_pId = "extra-menu-572";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_572].m_Function = [&]() {
        return m_aExtraMenuActive[571];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_573].m_pId = "extra-menu-573";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_573].m_Function = [&]() {
        return m_aExtraMenuActive[572];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_574].m_pId = "extra-menu-574";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_574].m_Function = [&]() {
        return m_aExtraMenuActive[573];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_575].m_pId = "extra-menu-575";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_575].m_Function = [&]() {
        return m_aExtraMenuActive[574];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_576].m_pId = "extra-menu-576";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_576].m_Function = [&]() {
        return m_aExtraMenuActive[575];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_577].m_pId = "extra-menu-577";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_577].m_Function = [&]() {
        return m_aExtraMenuActive[576];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_578].m_pId = "extra-menu-578";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_578].m_Function = [&]() {
        return m_aExtraMenuActive[577];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_579].m_pId = "extra-menu-579";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_579].m_Function = [&]() {
        return m_aExtraMenuActive[578];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_580].m_pId = "extra-menu-580";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_580].m_Function = [&]() {
        return m_aExtraMenuActive[579];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_581].m_pId = "extra-menu-581";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_581].m_Function = [&]() {
        return m_aExtraMenuActive[580];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_582].m_pId = "extra-menu-582";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_582].m_Function = [&]() {
        return m_aExtraMenuActive[581];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_583].m_pId = "extra-menu-583";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_583].m_Function = [&]() {
        return m_aExtraMenuActive[582];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_584].m_pId = "extra-menu-584";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_584].m_Function = [&]() {
        return m_aExtraMenuActive[583];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_585].m_pId = "extra-menu-585";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_585].m_Function = [&]() {
        return m_aExtraMenuActive[584];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_586].m_pId = "extra-menu-586";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_586].m_Function = [&]() {
        return m_aExtraMenuActive[585];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_587].m_pId = "extra-menu-587";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_587].m_Function = [&]() {
        return m_aExtraMenuActive[586];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_588].m_pId = "extra-menu-588";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_588].m_Function = [&]() {
        return m_aExtraMenuActive[587];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_589].m_pId = "extra-menu-589";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_589].m_Function = [&]() {
        return m_aExtraMenuActive[588];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_590].m_pId = "extra-menu-590";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_590].m_Function = [&]() {
        return m_aExtraMenuActive[589];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_591].m_pId = "extra-menu-591";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_591].m_Function = [&]() {
        return m_aExtraMenuActive[590];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_592].m_pId = "extra-menu-592";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_592].m_Function = [&]() {
        return m_aExtraMenuActive[591];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_593].m_pId = "extra-menu-593";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_593].m_Function = [&]() {
        return m_aExtraMenuActive[592];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_594].m_pId = "extra-menu-594";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_594].m_Function = [&]() {
        return m_aExtraMenuActive[593];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_595].m_pId = "extra-menu-595";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_595].m_Function = [&]() {
        return m_aExtraMenuActive[594];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_596].m_pId = "extra-menu-596";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_596].m_Function = [&]() {
        return m_aExtraMenuActive[595];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_597].m_pId = "extra-menu-597";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_597].m_Function = [&]() {
        return m_aExtraMenuActive[596];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_598].m_pId = "extra-menu-598";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_598].m_Function = [&]() {
        return m_aExtraMenuActive[597];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_599].m_pId = "extra-menu-599";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_599].m_Function = [&]() {
        return m_aExtraMenuActive[598];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_600].m_pId = "extra-menu-600";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_600].m_Function = [&]() {
        return m_aExtraMenuActive[599];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_601].m_pId = "extra-menu-601";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_601].m_Function = [&]() {
        return m_aExtraMenuActive[600];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_602].m_pId = "extra-menu-602";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_602].m_Function = [&]() {
        return m_aExtraMenuActive[601];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_603].m_pId = "extra-menu-603";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_603].m_Function = [&]() {
        return m_aExtraMenuActive[602];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_604].m_pId = "extra-menu-604";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_604].m_Function = [&]() {
        return m_aExtraMenuActive[603];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_605].m_pId = "extra-menu-605";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_605].m_Function = [&]() {
        return m_aExtraMenuActive[604];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_606].m_pId = "extra-menu-606";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_606].m_Function = [&]() {
        return m_aExtraMenuActive[605];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_607].m_pId = "extra-menu-607";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_607].m_Function = [&]() {
        return m_aExtraMenuActive[606];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_608].m_pId = "extra-menu-608";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_608].m_Function = [&]() {
        return m_aExtraMenuActive[607];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_609].m_pId = "extra-menu-609";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_609].m_Function = [&]() {
        return m_aExtraMenuActive[608];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_610].m_pId = "extra-menu-610";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_610].m_Function = [&]() {
        return m_aExtraMenuActive[609];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_611].m_pId = "extra-menu-611";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_611].m_Function = [&]() {
        return m_aExtraMenuActive[610];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_612].m_pId = "extra-menu-612";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_612].m_Function = [&]() {
        return m_aExtraMenuActive[611];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_613].m_pId = "extra-menu-613";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_613].m_Function = [&]() {
        return m_aExtraMenuActive[612];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_614].m_pId = "extra-menu-614";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_614].m_Function = [&]() {
        return m_aExtraMenuActive[613];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_615].m_pId = "extra-menu-615";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_615].m_Function = [&]() {
        return m_aExtraMenuActive[614];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_616].m_pId = "extra-menu-616";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_616].m_Function = [&]() {
        return m_aExtraMenuActive[615];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_617].m_pId = "extra-menu-617";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_617].m_Function = [&]() {
        return m_aExtraMenuActive[616];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_618].m_pId = "extra-menu-618";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_618].m_Function = [&]() {
        return m_aExtraMenuActive[617];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_619].m_pId = "extra-menu-619";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_619].m_Function = [&]() {
        return m_aExtraMenuActive[618];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_620].m_pId = "extra-menu-620";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_620].m_Function = [&]() {
        return m_aExtraMenuActive[619];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_621].m_pId = "extra-menu-621";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_621].m_Function = [&]() {
        return m_aExtraMenuActive[620];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_622].m_pId = "extra-menu-622";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_622].m_Function = [&]() {
        return m_aExtraMenuActive[621];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_623].m_pId = "extra-menu-623";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_623].m_Function = [&]() {
        return m_aExtraMenuActive[622];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_624].m_pId = "extra-menu-624";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_624].m_Function = [&]() {
        return m_aExtraMenuActive[623];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_625].m_pId = "extra-menu-625";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_625].m_Function = [&]() {
        return m_aExtraMenuActive[624];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_626].m_pId = "extra-menu-626";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_626].m_Function = [&]() {
        return m_aExtraMenuActive[625];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_627].m_pId = "extra-menu-627";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_627].m_Function = [&]() {
        return m_aExtraMenuActive[626];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_628].m_pId = "extra-menu-628";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_628].m_Function = [&]() {
        return m_aExtraMenuActive[627];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_629].m_pId = "extra-menu-629";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_629].m_Function = [&]() {
        return m_aExtraMenuActive[628];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_630].m_pId = "extra-menu-630";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_630].m_Function = [&]() {
        return m_aExtraMenuActive[629];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_631].m_pId = "extra-menu-631";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_631].m_Function = [&]() {
        return m_aExtraMenuActive[630];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_632].m_pId = "extra-menu-632";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_632].m_Function = [&]() {
        return m_aExtraMenuActive[631];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_633].m_pId = "extra-menu-633";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_633].m_Function = [&]() {
        return m_aExtraMenuActive[632];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_634].m_pId = "extra-menu-634";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_634].m_Function = [&]() {
        return m_aExtraMenuActive[633];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_635].m_pId = "extra-menu-635";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_635].m_Function = [&]() {
        return m_aExtraMenuActive[634];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_636].m_pId = "extra-menu-636";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_636].m_Function = [&]() {
        return m_aExtraMenuActive[635];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_637].m_pId = "extra-menu-637";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_637].m_Function = [&]() {
        return m_aExtraMenuActive[636];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_638].m_pId = "extra-menu-638";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_638].m_Function = [&]() {
        return m_aExtraMenuActive[637];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_639].m_pId = "extra-menu-639";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_639].m_Function = [&]() {
        return m_aExtraMenuActive[638];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_640].m_pId = "extra-menu-640";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_640].m_Function = [&]() {
        return m_aExtraMenuActive[639];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_641].m_pId = "extra-menu-641";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_641].m_Function = [&]() {
        return m_aExtraMenuActive[640];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_642].m_pId = "extra-menu-642";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_642].m_Function = [&]() {
        return m_aExtraMenuActive[641];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_643].m_pId = "extra-menu-643";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_643].m_Function = [&]() {
        return m_aExtraMenuActive[642];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_644].m_pId = "extra-menu-644";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_644].m_Function = [&]() {
        return m_aExtraMenuActive[643];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_645].m_pId = "extra-menu-645";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_645].m_Function = [&]() {
        return m_aExtraMenuActive[644];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_646].m_pId = "extra-menu-646";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_646].m_Function = [&]() {
        return m_aExtraMenuActive[645];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_647].m_pId = "extra-menu-647";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_647].m_Function = [&]() {
        return m_aExtraMenuActive[646];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_648].m_pId = "extra-menu-648";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_648].m_Function = [&]() {
        return m_aExtraMenuActive[647];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_649].m_pId = "extra-menu-649";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_649].m_Function = [&]() {
        return m_aExtraMenuActive[648];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_650].m_pId = "extra-menu-650";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_650].m_Function = [&]() {
        return m_aExtraMenuActive[649];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_651].m_pId = "extra-menu-651";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_651].m_Function = [&]() {
        return m_aExtraMenuActive[650];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_652].m_pId = "extra-menu-652";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_652].m_Function = [&]() {
        return m_aExtraMenuActive[651];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_653].m_pId = "extra-menu-653";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_653].m_Function = [&]() {
        return m_aExtraMenuActive[652];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_654].m_pId = "extra-menu-654";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_654].m_Function = [&]() {
        return m_aExtraMenuActive[653];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_655].m_pId = "extra-menu-655";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_655].m_Function = [&]() {
        return m_aExtraMenuActive[654];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_656].m_pId = "extra-menu-656";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_656].m_Function = [&]() {
        return m_aExtraMenuActive[655];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_657].m_pId = "extra-menu-657";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_657].m_Function = [&]() {
        return m_aExtraMenuActive[656];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_658].m_pId = "extra-menu-658";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_658].m_Function = [&]() {
        return m_aExtraMenuActive[657];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_659].m_pId = "extra-menu-659";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_659].m_Function = [&]() {
        return m_aExtraMenuActive[658];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_660].m_pId = "extra-menu-660";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_660].m_Function = [&]() {
        return m_aExtraMenuActive[659];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_661].m_pId = "extra-menu-661";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_661].m_Function = [&]() {
        return m_aExtraMenuActive[660];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_662].m_pId = "extra-menu-662";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_662].m_Function = [&]() {
        return m_aExtraMenuActive[661];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_663].m_pId = "extra-menu-663";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_663].m_Function = [&]() {
        return m_aExtraMenuActive[662];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_664].m_pId = "extra-menu-664";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_664].m_Function = [&]() {
        return m_aExtraMenuActive[663];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_665].m_pId = "extra-menu-665";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_665].m_Function = [&]() {
        return m_aExtraMenuActive[664];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_666].m_pId = "extra-menu-666";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_666].m_Function = [&]() {
        return m_aExtraMenuActive[665];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_667].m_pId = "extra-menu-667";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_667].m_Function = [&]() {
        return m_aExtraMenuActive[666];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_668].m_pId = "extra-menu-668";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_668].m_Function = [&]() {
        return m_aExtraMenuActive[667];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_669].m_pId = "extra-menu-669";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_669].m_Function = [&]() {
        return m_aExtraMenuActive[668];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_670].m_pId = "extra-menu-670";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_670].m_Function = [&]() {
        return m_aExtraMenuActive[669];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_671].m_pId = "extra-menu-671";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_671].m_Function = [&]() {
        return m_aExtraMenuActive[670];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_672].m_pId = "extra-menu-672";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_672].m_Function = [&]() {
        return m_aExtraMenuActive[671];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_673].m_pId = "extra-menu-673";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_673].m_Function = [&]() {
        return m_aExtraMenuActive[672];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_674].m_pId = "extra-menu-674";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_674].m_Function = [&]() {
        return m_aExtraMenuActive[673];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_675].m_pId = "extra-menu-675";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_675].m_Function = [&]() {
        return m_aExtraMenuActive[674];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_676].m_pId = "extra-menu-676";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_676].m_Function = [&]() {
        return m_aExtraMenuActive[675];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_677].m_pId = "extra-menu-677";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_677].m_Function = [&]() {
        return m_aExtraMenuActive[676];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_678].m_pId = "extra-menu-678";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_678].m_Function = [&]() {
        return m_aExtraMenuActive[677];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_679].m_pId = "extra-menu-679";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_679].m_Function = [&]() {
        return m_aExtraMenuActive[678];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_680].m_pId = "extra-menu-680";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_680].m_Function = [&]() {
        return m_aExtraMenuActive[679];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_681].m_pId = "extra-menu-681";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_681].m_Function = [&]() {
        return m_aExtraMenuActive[680];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_682].m_pId = "extra-menu-682";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_682].m_Function = [&]() {
        return m_aExtraMenuActive[681];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_683].m_pId = "extra-menu-683";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_683].m_Function = [&]() {
        return m_aExtraMenuActive[682];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_684].m_pId = "extra-menu-684";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_684].m_Function = [&]() {
        return m_aExtraMenuActive[683];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_685].m_pId = "extra-menu-685";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_685].m_Function = [&]() {
        return m_aExtraMenuActive[684];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_686].m_pId = "extra-menu-686";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_686].m_Function = [&]() {
        return m_aExtraMenuActive[685];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_687].m_pId = "extra-menu-687";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_687].m_Function = [&]() {
        return m_aExtraMenuActive[686];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_688].m_pId = "extra-menu-688";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_688].m_Function = [&]() {
        return m_aExtraMenuActive[687];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_689].m_pId = "extra-menu-689";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_689].m_Function = [&]() {
        return m_aExtraMenuActive[688];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_690].m_pId = "extra-menu-690";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_690].m_Function = [&]() {
        return m_aExtraMenuActive[689];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_691].m_pId = "extra-menu-691";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_691].m_Function = [&]() {
        return m_aExtraMenuActive[690];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_692].m_pId = "extra-menu-692";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_692].m_Function = [&]() {
        return m_aExtraMenuActive[691];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_693].m_pId = "extra-menu-693";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_693].m_Function = [&]() {
        return m_aExtraMenuActive[692];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_694].m_pId = "extra-menu-694";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_694].m_Function = [&]() {
        return m_aExtraMenuActive[693];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_695].m_pId = "extra-menu-695";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_695].m_Function = [&]() {
        return m_aExtraMenuActive[694];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_696].m_pId = "extra-menu-696";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_696].m_Function = [&]() {
        return m_aExtraMenuActive[695];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_697].m_pId = "extra-menu-697";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_697].m_Function = [&]() {
        return m_aExtraMenuActive[696];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_698].m_pId = "extra-menu-698";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_698].m_Function = [&]() {
        return m_aExtraMenuActive[697];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_699].m_pId = "extra-menu-699";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_699].m_Function = [&]() {
        return m_aExtraMenuActive[698];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_700].m_pId = "extra-menu-700";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_700].m_Function = [&]() {
        return m_aExtraMenuActive[699];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_701].m_pId = "extra-menu-701";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_701].m_Function = [&]() {
        return m_aExtraMenuActive[700];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_702].m_pId = "extra-menu-702";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_702].m_Function = [&]() {
        return m_aExtraMenuActive[701];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_703].m_pId = "extra-menu-703";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_703].m_Function = [&]() {
        return m_aExtraMenuActive[702];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_704].m_pId = "extra-menu-704";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_704].m_Function = [&]() {
        return m_aExtraMenuActive[703];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_705].m_pId = "extra-menu-705";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_705].m_Function = [&]() {
        return m_aExtraMenuActive[704];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_706].m_pId = "extra-menu-706";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_706].m_Function = [&]() {
        return m_aExtraMenuActive[705];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_707].m_pId = "extra-menu-707";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_707].m_Function = [&]() {
        return m_aExtraMenuActive[706];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_708].m_pId = "extra-menu-708";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_708].m_Function = [&]() {
        return m_aExtraMenuActive[707];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_709].m_pId = "extra-menu-709";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_709].m_Function = [&]() {
        return m_aExtraMenuActive[708];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_710].m_pId = "extra-menu-710";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_710].m_Function = [&]() {
        return m_aExtraMenuActive[709];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_711].m_pId = "extra-menu-711";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_711].m_Function = [&]() {
        return m_aExtraMenuActive[710];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_712].m_pId = "extra-menu-712";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_712].m_Function = [&]() {
        return m_aExtraMenuActive[711];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_713].m_pId = "extra-menu-713";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_713].m_Function = [&]() {
        return m_aExtraMenuActive[712];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_714].m_pId = "extra-menu-714";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_714].m_Function = [&]() {
        return m_aExtraMenuActive[713];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_715].m_pId = "extra-menu-715";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_715].m_Function = [&]() {
        return m_aExtraMenuActive[714];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_716].m_pId = "extra-menu-716";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_716].m_Function = [&]() {
        return m_aExtraMenuActive[715];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_717].m_pId = "extra-menu-717";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_717].m_Function = [&]() {
        return m_aExtraMenuActive[716];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_718].m_pId = "extra-menu-718";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_718].m_Function = [&]() {
        return m_aExtraMenuActive[717];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_719].m_pId = "extra-menu-719";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_719].m_Function = [&]() {
        return m_aExtraMenuActive[718];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_720].m_pId = "extra-menu-720";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_720].m_Function = [&]() {
        return m_aExtraMenuActive[719];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_721].m_pId = "extra-menu-721";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_721].m_Function = [&]() {
        return m_aExtraMenuActive[720];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_722].m_pId = "extra-menu-722";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_722].m_Function = [&]() {
        return m_aExtraMenuActive[721];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_723].m_pId = "extra-menu-723";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_723].m_Function = [&]() {
        return m_aExtraMenuActive[722];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_724].m_pId = "extra-menu-724";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_724].m_Function = [&]() {
        return m_aExtraMenuActive[723];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_725].m_pId = "extra-menu-725";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_725].m_Function = [&]() {
        return m_aExtraMenuActive[724];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_726].m_pId = "extra-menu-726";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_726].m_Function = [&]() {
        return m_aExtraMenuActive[725];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_727].m_pId = "extra-menu-727";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_727].m_Function = [&]() {
        return m_aExtraMenuActive[726];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_728].m_pId = "extra-menu-728";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_728].m_Function = [&]() {
        return m_aExtraMenuActive[727];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_729].m_pId = "extra-menu-729";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_729].m_Function = [&]() {
        return m_aExtraMenuActive[728];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_730].m_pId = "extra-menu-730";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_730].m_Function = [&]() {
        return m_aExtraMenuActive[729];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_731].m_pId = "extra-menu-731";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_731].m_Function = [&]() {
        return m_aExtraMenuActive[730];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_732].m_pId = "extra-menu-732";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_732].m_Function = [&]() {
        return m_aExtraMenuActive[731];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_733].m_pId = "extra-menu-733";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_733].m_Function = [&]() {
        return m_aExtraMenuActive[732];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_734].m_pId = "extra-menu-734";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_734].m_Function = [&]() {
        return m_aExtraMenuActive[733];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_735].m_pId = "extra-menu-735";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_735].m_Function = [&]() {
        return m_aExtraMenuActive[734];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_736].m_pId = "extra-menu-736";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_736].m_Function = [&]() {
        return m_aExtraMenuActive[735];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_737].m_pId = "extra-menu-737";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_737].m_Function = [&]() {
        return m_aExtraMenuActive[736];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_738].m_pId = "extra-menu-738";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_738].m_Function = [&]() {
        return m_aExtraMenuActive[737];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_739].m_pId = "extra-menu-739";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_739].m_Function = [&]() {
        return m_aExtraMenuActive[738];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_740].m_pId = "extra-menu-740";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_740].m_Function = [&]() {
        return m_aExtraMenuActive[739];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_741].m_pId = "extra-menu-741";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_741].m_Function = [&]() {
        return m_aExtraMenuActive[740];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_742].m_pId = "extra-menu-742";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_742].m_Function = [&]() {
        return m_aExtraMenuActive[741];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_743].m_pId = "extra-menu-743";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_743].m_Function = [&]() {
        return m_aExtraMenuActive[742];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_744].m_pId = "extra-menu-744";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_744].m_Function = [&]() {
        return m_aExtraMenuActive[743];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_745].m_pId = "extra-menu-745";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_745].m_Function = [&]() {
        return m_aExtraMenuActive[744];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_746].m_pId = "extra-menu-746";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_746].m_Function = [&]() {
        return m_aExtraMenuActive[745];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_747].m_pId = "extra-menu-747";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_747].m_Function = [&]() {
        return m_aExtraMenuActive[746];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_748].m_pId = "extra-menu-748";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_748].m_Function = [&]() {
        return m_aExtraMenuActive[747];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_749].m_pId = "extra-menu-749";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_749].m_Function = [&]() {
        return m_aExtraMenuActive[748];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_750].m_pId = "extra-menu-750";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_750].m_Function = [&]() {
        return m_aExtraMenuActive[749];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_751].m_pId = "extra-menu-751";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_751].m_Function = [&]() {
        return m_aExtraMenuActive[750];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_752].m_pId = "extra-menu-752";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_752].m_Function = [&]() {
        return m_aExtraMenuActive[751];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_753].m_pId = "extra-menu-753";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_753].m_Function = [&]() {
        return m_aExtraMenuActive[752];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_754].m_pId = "extra-menu-754";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_754].m_Function = [&]() {
        return m_aExtraMenuActive[753];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_755].m_pId = "extra-menu-755";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_755].m_Function = [&]() {
        return m_aExtraMenuActive[754];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_756].m_pId = "extra-menu-756";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_756].m_Function = [&]() {
        return m_aExtraMenuActive[755];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_757].m_pId = "extra-menu-757";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_757].m_Function = [&]() {
        return m_aExtraMenuActive[756];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_758].m_pId = "extra-menu-758";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_758].m_Function = [&]() {
        return m_aExtraMenuActive[757];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_759].m_pId = "extra-menu-759";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_759].m_Function = [&]() {
        return m_aExtraMenuActive[758];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_760].m_pId = "extra-menu-760";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_760].m_Function = [&]() {
        return m_aExtraMenuActive[759];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_761].m_pId = "extra-menu-761";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_761].m_Function = [&]() {
        return m_aExtraMenuActive[760];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_762].m_pId = "extra-menu-762";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_762].m_Function = [&]() {
        return m_aExtraMenuActive[761];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_763].m_pId = "extra-menu-763";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_763].m_Function = [&]() {
        return m_aExtraMenuActive[762];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_764].m_pId = "extra-menu-764";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_764].m_Function = [&]() {
        return m_aExtraMenuActive[763];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_765].m_pId = "extra-menu-765";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_765].m_Function = [&]() {
        return m_aExtraMenuActive[764];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_766].m_pId = "extra-menu-766";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_766].m_Function = [&]() {
        return m_aExtraMenuActive[765];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_767].m_pId = "extra-menu-767";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_767].m_Function = [&]() {
        return m_aExtraMenuActive[766];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_768].m_pId = "extra-menu-768";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_768].m_Function = [&]() {
        return m_aExtraMenuActive[767];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_769].m_pId = "extra-menu-769";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_769].m_Function = [&]() {
        return m_aExtraMenuActive[768];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_770].m_pId = "extra-menu-770";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_770].m_Function = [&]() {
        return m_aExtraMenuActive[769];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_771].m_pId = "extra-menu-771";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_771].m_Function = [&]() {
        return m_aExtraMenuActive[770];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_772].m_pId = "extra-menu-772";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_772].m_Function = [&]() {
        return m_aExtraMenuActive[771];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_773].m_pId = "extra-menu-773";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_773].m_Function = [&]() {
        return m_aExtraMenuActive[772];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_774].m_pId = "extra-menu-774";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_774].m_Function = [&]() {
        return m_aExtraMenuActive[773];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_775].m_pId = "extra-menu-775";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_775].m_Function = [&]() {
        return m_aExtraMenuActive[774];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_776].m_pId = "extra-menu-776";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_776].m_Function = [&]() {
        return m_aExtraMenuActive[775];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_777].m_pId = "extra-menu-777";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_777].m_Function = [&]() {
        return m_aExtraMenuActive[776];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_778].m_pId = "extra-menu-778";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_778].m_Function = [&]() {
        return m_aExtraMenuActive[777];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_779].m_pId = "extra-menu-779";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_779].m_Function = [&]() {
        return m_aExtraMenuActive[778];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_780].m_pId = "extra-menu-780";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_780].m_Function = [&]() {
        return m_aExtraMenuActive[779];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_781].m_pId = "extra-menu-781";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_781].m_Function = [&]() {
        return m_aExtraMenuActive[780];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_782].m_pId = "extra-menu-782";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_782].m_Function = [&]() {
        return m_aExtraMenuActive[781];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_783].m_pId = "extra-menu-783";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_783].m_Function = [&]() {
        return m_aExtraMenuActive[782];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_784].m_pId = "extra-menu-784";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_784].m_Function = [&]() {
        return m_aExtraMenuActive[783];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_785].m_pId = "extra-menu-785";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_785].m_Function = [&]() {
        return m_aExtraMenuActive[784];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_786].m_pId = "extra-menu-786";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_786].m_Function = [&]() {
        return m_aExtraMenuActive[785];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_787].m_pId = "extra-menu-787";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_787].m_Function = [&]() {
        return m_aExtraMenuActive[786];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_788].m_pId = "extra-menu-788";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_788].m_Function = [&]() {
        return m_aExtraMenuActive[787];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_789].m_pId = "extra-menu-789";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_789].m_Function = [&]() {
        return m_aExtraMenuActive[788];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_790].m_pId = "extra-menu-790";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_790].m_Function = [&]() {
        return m_aExtraMenuActive[789];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_791].m_pId = "extra-menu-791";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_791].m_Function = [&]() {
        return m_aExtraMenuActive[790];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_792].m_pId = "extra-menu-792";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_792].m_Function = [&]() {
        return m_aExtraMenuActive[791];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_793].m_pId = "extra-menu-793";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_793].m_Function = [&]() {
        return m_aExtraMenuActive[792];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_794].m_pId = "extra-menu-794";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_794].m_Function = [&]() {
        return m_aExtraMenuActive[793];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_795].m_pId = "extra-menu-795";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_795].m_Function = [&]() {
        return m_aExtraMenuActive[794];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_796].m_pId = "extra-menu-796";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_796].m_Function = [&]() {
        return m_aExtraMenuActive[795];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_797].m_pId = "extra-menu-797";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_797].m_Function = [&]() {
        return m_aExtraMenuActive[796];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_798].m_pId = "extra-menu-798";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_798].m_Function = [&]() {
        return m_aExtraMenuActive[797];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_799].m_pId = "extra-menu-799";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_799].m_Function = [&]() {
        return m_aExtraMenuActive[798];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_800].m_pId = "extra-menu-800";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_800].m_Function = [&]() {
        return m_aExtraMenuActive[799];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_801].m_pId = "extra-menu-801";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_801].m_Function = [&]() {
        return m_aExtraMenuActive[800];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_802].m_pId = "extra-menu-802";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_802].m_Function = [&]() {
        return m_aExtraMenuActive[801];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_803].m_pId = "extra-menu-803";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_803].m_Function = [&]() {
        return m_aExtraMenuActive[802];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_804].m_pId = "extra-menu-804";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_804].m_Function = [&]() {
        return m_aExtraMenuActive[803];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_805].m_pId = "extra-menu-805";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_805].m_Function = [&]() {
        return m_aExtraMenuActive[804];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_806].m_pId = "extra-menu-806";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_806].m_Function = [&]() {
        return m_aExtraMenuActive[805];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_807].m_pId = "extra-menu-807";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_807].m_Function = [&]() {
        return m_aExtraMenuActive[806];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_808].m_pId = "extra-menu-808";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_808].m_Function = [&]() {
        return m_aExtraMenuActive[807];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_809].m_pId = "extra-menu-809";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_809].m_Function = [&]() {
        return m_aExtraMenuActive[808];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_810].m_pId = "extra-menu-810";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_810].m_Function = [&]() {
        return m_aExtraMenuActive[809];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_811].m_pId = "extra-menu-811";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_811].m_Function = [&]() {
        return m_aExtraMenuActive[810];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_812].m_pId = "extra-menu-812";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_812].m_Function = [&]() {
        return m_aExtraMenuActive[811];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_813].m_pId = "extra-menu-813";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_813].m_Function = [&]() {
        return m_aExtraMenuActive[812];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_814].m_pId = "extra-menu-814";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_814].m_Function = [&]() {
        return m_aExtraMenuActive[813];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_815].m_pId = "extra-menu-815";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_815].m_Function = [&]() {
        return m_aExtraMenuActive[814];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_816].m_pId = "extra-menu-816";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_816].m_Function = [&]() {
        return m_aExtraMenuActive[815];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_817].m_pId = "extra-menu-817";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_817].m_Function = [&]() {
        return m_aExtraMenuActive[816];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_818].m_pId = "extra-menu-818";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_818].m_Function = [&]() {
        return m_aExtraMenuActive[817];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_819].m_pId = "extra-menu-819";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_819].m_Function = [&]() {
        return m_aExtraMenuActive[818];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_820].m_pId = "extra-menu-820";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_820].m_Function = [&]() {
        return m_aExtraMenuActive[819];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_821].m_pId = "extra-menu-821";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_821].m_Function = [&]() {
        return m_aExtraMenuActive[820];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_822].m_pId = "extra-menu-822";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_822].m_Function = [&]() {
        return m_aExtraMenuActive[821];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_823].m_pId = "extra-menu-823";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_823].m_Function = [&]() {
        return m_aExtraMenuActive[822];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_824].m_pId = "extra-menu-824";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_824].m_Function = [&]() {
        return m_aExtraMenuActive[823];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_825].m_pId = "extra-menu-825";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_825].m_Function = [&]() {
        return m_aExtraMenuActive[824];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_826].m_pId = "extra-menu-826";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_826].m_Function = [&]() {
        return m_aExtraMenuActive[825];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_827].m_pId = "extra-menu-827";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_827].m_Function = [&]() {
        return m_aExtraMenuActive[826];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_828].m_pId = "extra-menu-828";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_828].m_Function = [&]() {
        return m_aExtraMenuActive[827];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_829].m_pId = "extra-menu-829";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_829].m_Function = [&]() {
        return m_aExtraMenuActive[828];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_830].m_pId = "extra-menu-830";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_830].m_Function = [&]() {
        return m_aExtraMenuActive[829];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_831].m_pId = "extra-menu-831";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_831].m_Function = [&]() {
        return m_aExtraMenuActive[830];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_832].m_pId = "extra-menu-832";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_832].m_Function = [&]() {
        return m_aExtraMenuActive[831];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_833].m_pId = "extra-menu-833";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_833].m_Function = [&]() {
        return m_aExtraMenuActive[832];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_834].m_pId = "extra-menu-834";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_834].m_Function = [&]() {
        return m_aExtraMenuActive[833];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_835].m_pId = "extra-menu-835";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_835].m_Function = [&]() {
        return m_aExtraMenuActive[834];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_836].m_pId = "extra-menu-836";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_836].m_Function = [&]() {
        return m_aExtraMenuActive[835];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_837].m_pId = "extra-menu-837";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_837].m_Function = [&]() {
        return m_aExtraMenuActive[836];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_838].m_pId = "extra-menu-838";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_838].m_Function = [&]() {
        return m_aExtraMenuActive[837];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_839].m_pId = "extra-menu-839";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_839].m_Function = [&]() {
        return m_aExtraMenuActive[838];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_840].m_pId = "extra-menu-840";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_840].m_Function = [&]() {
        return m_aExtraMenuActive[839];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_841].m_pId = "extra-menu-841";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_841].m_Function = [&]() {
        return m_aExtraMenuActive[840];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_842].m_pId = "extra-menu-842";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_842].m_Function = [&]() {
        return m_aExtraMenuActive[841];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_843].m_pId = "extra-menu-843";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_843].m_Function = [&]() {
        return m_aExtraMenuActive[842];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_844].m_pId = "extra-menu-844";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_844].m_Function = [&]() {
        return m_aExtraMenuActive[843];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_845].m_pId = "extra-menu-845";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_845].m_Function = [&]() {
        return m_aExtraMenuActive[844];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_846].m_pId = "extra-menu-846";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_846].m_Function = [&]() {
        return m_aExtraMenuActive[845];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_847].m_pId = "extra-menu-847";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_847].m_Function = [&]() {
        return m_aExtraMenuActive[846];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_848].m_pId = "extra-menu-848";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_848].m_Function = [&]() {
        return m_aExtraMenuActive[847];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_849].m_pId = "extra-menu-849";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_849].m_Function = [&]() {
        return m_aExtraMenuActive[848];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_850].m_pId = "extra-menu-850";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_850].m_Function = [&]() {
        return m_aExtraMenuActive[849];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_851].m_pId = "extra-menu-851";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_851].m_Function = [&]() {
        return m_aExtraMenuActive[850];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_852].m_pId = "extra-menu-852";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_852].m_Function = [&]() {
        return m_aExtraMenuActive[851];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_853].m_pId = "extra-menu-853";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_853].m_Function = [&]() {
        return m_aExtraMenuActive[852];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_854].m_pId = "extra-menu-854";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_854].m_Function = [&]() {
        return m_aExtraMenuActive[853];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_855].m_pId = "extra-menu-855";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_855].m_Function = [&]() {
        return m_aExtraMenuActive[854];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_856].m_pId = "extra-menu-856";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_856].m_Function = [&]() {
        return m_aExtraMenuActive[855];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_857].m_pId = "extra-menu-857";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_857].m_Function = [&]() {
        return m_aExtraMenuActive[856];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_858].m_pId = "extra-menu-858";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_858].m_Function = [&]() {
        return m_aExtraMenuActive[857];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_859].m_pId = "extra-menu-859";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_859].m_Function = [&]() {
        return m_aExtraMenuActive[858];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_860].m_pId = "extra-menu-860";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_860].m_Function = [&]() {
        return m_aExtraMenuActive[859];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_861].m_pId = "extra-menu-861";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_861].m_Function = [&]() {
        return m_aExtraMenuActive[860];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_862].m_pId = "extra-menu-862";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_862].m_Function = [&]() {
        return m_aExtraMenuActive[861];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_863].m_pId = "extra-menu-863";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_863].m_Function = [&]() {
        return m_aExtraMenuActive[862];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_864].m_pId = "extra-menu-864";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_864].m_Function = [&]() {
        return m_aExtraMenuActive[863];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_865].m_pId = "extra-menu-865";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_865].m_Function = [&]() {
        return m_aExtraMenuActive[864];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_866].m_pId = "extra-menu-866";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_866].m_Function = [&]() {
        return m_aExtraMenuActive[865];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_867].m_pId = "extra-menu-867";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_867].m_Function = [&]() {
        return m_aExtraMenuActive[866];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_868].m_pId = "extra-menu-868";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_868].m_Function = [&]() {
        return m_aExtraMenuActive[867];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_869].m_pId = "extra-menu-869";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_869].m_Function = [&]() {
        return m_aExtraMenuActive[868];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_870].m_pId = "extra-menu-870";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_870].m_Function = [&]() {
        return m_aExtraMenuActive[869];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_871].m_pId = "extra-menu-871";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_871].m_Function = [&]() {
        return m_aExtraMenuActive[870];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_872].m_pId = "extra-menu-872";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_872].m_Function = [&]() {
        return m_aExtraMenuActive[871];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_873].m_pId = "extra-menu-873";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_873].m_Function = [&]() {
        return m_aExtraMenuActive[872];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_874].m_pId = "extra-menu-874";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_874].m_Function = [&]() {
        return m_aExtraMenuActive[873];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_875].m_pId = "extra-menu-875";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_875].m_Function = [&]() {
        return m_aExtraMenuActive[874];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_876].m_pId = "extra-menu-876";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_876].m_Function = [&]() {
        return m_aExtraMenuActive[875];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_877].m_pId = "extra-menu-877";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_877].m_Function = [&]() {
        return m_aExtraMenuActive[876];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_878].m_pId = "extra-menu-878";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_878].m_Function = [&]() {
        return m_aExtraMenuActive[877];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_879].m_pId = "extra-menu-879";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_879].m_Function = [&]() {
        return m_aExtraMenuActive[878];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_880].m_pId = "extra-menu-880";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_880].m_Function = [&]() {
        return m_aExtraMenuActive[879];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_881].m_pId = "extra-menu-881";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_881].m_Function = [&]() {
        return m_aExtraMenuActive[880];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_882].m_pId = "extra-menu-882";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_882].m_Function = [&]() {
        return m_aExtraMenuActive[881];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_883].m_pId = "extra-menu-883";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_883].m_Function = [&]() {
        return m_aExtraMenuActive[882];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_884].m_pId = "extra-menu-884";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_884].m_Function = [&]() {
        return m_aExtraMenuActive[883];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_885].m_pId = "extra-menu-885";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_885].m_Function = [&]() {
        return m_aExtraMenuActive[884];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_886].m_pId = "extra-menu-886";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_886].m_Function = [&]() {
        return m_aExtraMenuActive[885];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_887].m_pId = "extra-menu-887";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_887].m_Function = [&]() {
        return m_aExtraMenuActive[886];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_888].m_pId = "extra-menu-888";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_888].m_Function = [&]() {
        return m_aExtraMenuActive[887];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_889].m_pId = "extra-menu-889";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_889].m_Function = [&]() {
        return m_aExtraMenuActive[888];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_890].m_pId = "extra-menu-890";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_890].m_Function = [&]() {
        return m_aExtraMenuActive[889];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_891].m_pId = "extra-menu-891";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_891].m_Function = [&]() {
        return m_aExtraMenuActive[890];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_892].m_pId = "extra-menu-892";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_892].m_Function = [&]() {
        return m_aExtraMenuActive[891];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_893].m_pId = "extra-menu-893";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_893].m_Function = [&]() {
        return m_aExtraMenuActive[892];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_894].m_pId = "extra-menu-894";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_894].m_Function = [&]() {
        return m_aExtraMenuActive[893];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_895].m_pId = "extra-menu-895";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_895].m_Function = [&]() {
        return m_aExtraMenuActive[894];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_896].m_pId = "extra-menu-896";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_896].m_Function = [&]() {
        return m_aExtraMenuActive[895];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_897].m_pId = "extra-menu-897";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_897].m_Function = [&]() {
        return m_aExtraMenuActive[896];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_898].m_pId = "extra-menu-898";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_898].m_Function = [&]() {
        return m_aExtraMenuActive[897];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_899].m_pId = "extra-menu-899";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_899].m_Function = [&]() {
        return m_aExtraMenuActive[898];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_900].m_pId = "extra-menu-900";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_900].m_Function = [&]() {
        return m_aExtraMenuActive[899];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_901].m_pId = "extra-menu-901";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_901].m_Function = [&]() {
        return m_aExtraMenuActive[900];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_902].m_pId = "extra-menu-902";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_902].m_Function = [&]() {
        return m_aExtraMenuActive[901];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_903].m_pId = "extra-menu-903";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_903].m_Function = [&]() {
        return m_aExtraMenuActive[902];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_904].m_pId = "extra-menu-904";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_904].m_Function = [&]() {
        return m_aExtraMenuActive[903];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_905].m_pId = "extra-menu-905";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_905].m_Function = [&]() {
        return m_aExtraMenuActive[904];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_906].m_pId = "extra-menu-906";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_906].m_Function = [&]() {
        return m_aExtraMenuActive[905];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_907].m_pId = "extra-menu-907";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_907].m_Function = [&]() {
        return m_aExtraMenuActive[906];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_908].m_pId = "extra-menu-908";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_908].m_Function = [&]() {
        return m_aExtraMenuActive[907];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_909].m_pId = "extra-menu-909";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_909].m_Function = [&]() {
        return m_aExtraMenuActive[908];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_910].m_pId = "extra-menu-910";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_910].m_Function = [&]() {
        return m_aExtraMenuActive[909];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_911].m_pId = "extra-menu-911";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_911].m_Function = [&]() {
        return m_aExtraMenuActive[910];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_912].m_pId = "extra-menu-912";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_912].m_Function = [&]() {
        return m_aExtraMenuActive[911];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_913].m_pId = "extra-menu-913";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_913].m_Function = [&]() {
        return m_aExtraMenuActive[912];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_914].m_pId = "extra-menu-914";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_914].m_Function = [&]() {
        return m_aExtraMenuActive[913];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_915].m_pId = "extra-menu-915";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_915].m_Function = [&]() {
        return m_aExtraMenuActive[914];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_916].m_pId = "extra-menu-916";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_916].m_Function = [&]() {
        return m_aExtraMenuActive[915];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_917].m_pId = "extra-menu-917";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_917].m_Function = [&]() {
        return m_aExtraMenuActive[916];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_918].m_pId = "extra-menu-918";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_918].m_Function = [&]() {
        return m_aExtraMenuActive[917];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_919].m_pId = "extra-menu-919";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_919].m_Function = [&]() {
        return m_aExtraMenuActive[918];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_920].m_pId = "extra-menu-920";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_920].m_Function = [&]() {
        return m_aExtraMenuActive[919];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_921].m_pId = "extra-menu-921";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_921].m_Function = [&]() {
        return m_aExtraMenuActive[920];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_922].m_pId = "extra-menu-922";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_922].m_Function = [&]() {
        return m_aExtraMenuActive[921];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_923].m_pId = "extra-menu-923";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_923].m_Function = [&]() {
        return m_aExtraMenuActive[922];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_924].m_pId = "extra-menu-924";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_924].m_Function = [&]() {
        return m_aExtraMenuActive[923];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_925].m_pId = "extra-menu-925";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_925].m_Function = [&]() {
        return m_aExtraMenuActive[924];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_926].m_pId = "extra-menu-926";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_926].m_Function = [&]() {
        return m_aExtraMenuActive[925];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_927].m_pId = "extra-menu-927";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_927].m_Function = [&]() {
        return m_aExtraMenuActive[926];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_928].m_pId = "extra-menu-928";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_928].m_Function = [&]() {
        return m_aExtraMenuActive[927];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_929].m_pId = "extra-menu-929";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_929].m_Function = [&]() {
        return m_aExtraMenuActive[928];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_930].m_pId = "extra-menu-930";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_930].m_Function = [&]() {
        return m_aExtraMenuActive[929];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_931].m_pId = "extra-menu-931";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_931].m_Function = [&]() {
        return m_aExtraMenuActive[930];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_932].m_pId = "extra-menu-932";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_932].m_Function = [&]() {
        return m_aExtraMenuActive[931];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_933].m_pId = "extra-menu-933";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_933].m_Function = [&]() {
        return m_aExtraMenuActive[932];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_934].m_pId = "extra-menu-934";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_934].m_Function = [&]() {
        return m_aExtraMenuActive[933];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_935].m_pId = "extra-menu-935";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_935].m_Function = [&]() {
        return m_aExtraMenuActive[934];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_936].m_pId = "extra-menu-936";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_936].m_Function = [&]() {
        return m_aExtraMenuActive[935];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_937].m_pId = "extra-menu-937";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_937].m_Function = [&]() {
        return m_aExtraMenuActive[936];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_938].m_pId = "extra-menu-938";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_938].m_Function = [&]() {
        return m_aExtraMenuActive[937];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_939].m_pId = "extra-menu-939";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_939].m_Function = [&]() {
        return m_aExtraMenuActive[938];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_940].m_pId = "extra-menu-940";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_940].m_Function = [&]() {
        return m_aExtraMenuActive[939];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_941].m_pId = "extra-menu-941";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_941].m_Function = [&]() {
        return m_aExtraMenuActive[940];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_942].m_pId = "extra-menu-942";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_942].m_Function = [&]() {
        return m_aExtraMenuActive[941];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_943].m_pId = "extra-menu-943";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_943].m_Function = [&]() {
        return m_aExtraMenuActive[942];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_944].m_pId = "extra-menu-944";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_944].m_Function = [&]() {
        return m_aExtraMenuActive[943];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_945].m_pId = "extra-menu-945";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_945].m_Function = [&]() {
        return m_aExtraMenuActive[944];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_946].m_pId = "extra-menu-946";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_946].m_Function = [&]() {
        return m_aExtraMenuActive[945];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_947].m_pId = "extra-menu-947";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_947].m_Function = [&]() {
        return m_aExtraMenuActive[946];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_948].m_pId = "extra-menu-948";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_948].m_Function = [&]() {
        return m_aExtraMenuActive[947];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_949].m_pId = "extra-menu-949";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_949].m_Function = [&]() {
        return m_aExtraMenuActive[948];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_950].m_pId = "extra-menu-950";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_950].m_Function = [&]() {
        return m_aExtraMenuActive[949];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_951].m_pId = "extra-menu-951";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_951].m_Function = [&]() {
        return m_aExtraMenuActive[950];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_952].m_pId = "extra-menu-952";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_952].m_Function = [&]() {
        return m_aExtraMenuActive[951];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_953].m_pId = "extra-menu-953";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_953].m_Function = [&]() {
        return m_aExtraMenuActive[952];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_954].m_pId = "extra-menu-954";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_954].m_Function = [&]() {
        return m_aExtraMenuActive[953];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_955].m_pId = "extra-menu-955";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_955].m_Function = [&]() {
        return m_aExtraMenuActive[954];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_956].m_pId = "extra-menu-956";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_956].m_Function = [&]() {
        return m_aExtraMenuActive[955];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_957].m_pId = "extra-menu-957";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_957].m_Function = [&]() {
        return m_aExtraMenuActive[956];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_958].m_pId = "extra-menu-958";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_958].m_Function = [&]() {
        return m_aExtraMenuActive[957];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_959].m_pId = "extra-menu-959";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_959].m_Function = [&]() {
        return m_aExtraMenuActive[958];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_960].m_pId = "extra-menu-960";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_960].m_Function = [&]() {
        return m_aExtraMenuActive[959];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_961].m_pId = "extra-menu-961";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_961].m_Function = [&]() {
        return m_aExtraMenuActive[960];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_962].m_pId = "extra-menu-962";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_962].m_Function = [&]() {
        return m_aExtraMenuActive[961];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_963].m_pId = "extra-menu-963";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_963].m_Function = [&]() {
        return m_aExtraMenuActive[962];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_964].m_pId = "extra-menu-964";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_964].m_Function = [&]() {
        return m_aExtraMenuActive[963];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_965].m_pId = "extra-menu-965";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_965].m_Function = [&]() {
        return m_aExtraMenuActive[964];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_966].m_pId = "extra-menu-966";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_966].m_Function = [&]() {
        return m_aExtraMenuActive[965];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_967].m_pId = "extra-menu-967";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_967].m_Function = [&]() {
        return m_aExtraMenuActive[966];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_968].m_pId = "extra-menu-968";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_968].m_Function = [&]() {
        return m_aExtraMenuActive[967];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_969].m_pId = "extra-menu-969";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_969].m_Function = [&]() {
        return m_aExtraMenuActive[968];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_970].m_pId = "extra-menu-970";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_970].m_Function = [&]() {
        return m_aExtraMenuActive[969];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_971].m_pId = "extra-menu-971";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_971].m_Function = [&]() {
        return m_aExtraMenuActive[970];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_972].m_pId = "extra-menu-972";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_972].m_Function = [&]() {
        return m_aExtraMenuActive[971];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_973].m_pId = "extra-menu-973";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_973].m_Function = [&]() {
        return m_aExtraMenuActive[972];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_974].m_pId = "extra-menu-974";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_974].m_Function = [&]() {
        return m_aExtraMenuActive[973];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_975].m_pId = "extra-menu-975";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_975].m_Function = [&]() {
        return m_aExtraMenuActive[974];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_976].m_pId = "extra-menu-976";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_976].m_Function = [&]() {
        return m_aExtraMenuActive[975];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_977].m_pId = "extra-menu-977";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_977].m_Function = [&]() {
        return m_aExtraMenuActive[976];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_978].m_pId = "extra-menu-978";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_978].m_Function = [&]() {
        return m_aExtraMenuActive[977];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_979].m_pId = "extra-menu-979";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_979].m_Function = [&]() {
        return m_aExtraMenuActive[978];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_980].m_pId = "extra-menu-980";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_980].m_Function = [&]() {
        return m_aExtraMenuActive[979];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_981].m_pId = "extra-menu-981";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_981].m_Function = [&]() {
        return m_aExtraMenuActive[980];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_982].m_pId = "extra-menu-982";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_982].m_Function = [&]() {
        return m_aExtraMenuActive[981];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_983].m_pId = "extra-menu-983";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_983].m_Function = [&]() {
        return m_aExtraMenuActive[982];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_984].m_pId = "extra-menu-984";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_984].m_Function = [&]() {
        return m_aExtraMenuActive[983];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_985].m_pId = "extra-menu-985";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_985].m_Function = [&]() {
        return m_aExtraMenuActive[984];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_986].m_pId = "extra-menu-986";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_986].m_Function = [&]() {
        return m_aExtraMenuActive[985];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_987].m_pId = "extra-menu-987";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_987].m_Function = [&]() {
        return m_aExtraMenuActive[986];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_988].m_pId = "extra-menu-988";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_988].m_Function = [&]() {
        return m_aExtraMenuActive[987];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_989].m_pId = "extra-menu-989";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_989].m_Function = [&]() {
        return m_aExtraMenuActive[988];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_990].m_pId = "extra-menu-990";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_990].m_Function = [&]() {
        return m_aExtraMenuActive[989];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_991].m_pId = "extra-menu-991";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_991].m_Function = [&]() {
        return m_aExtraMenuActive[990];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_992].m_pId = "extra-menu-992";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_992].m_Function = [&]() {
        return m_aExtraMenuActive[991];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_993].m_pId = "extra-menu-993";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_993].m_Function = [&]() {
        return m_aExtraMenuActive[992];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_994].m_pId = "extra-menu-994";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_994].m_Function = [&]() {
        return m_aExtraMenuActive[993];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_995].m_pId = "extra-menu-995";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_995].m_Function = [&]() {
        return m_aExtraMenuActive[994];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_996].m_pId = "extra-menu-996";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_996].m_Function = [&]() {
        return m_aExtraMenuActive[995];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_997].m_pId = "extra-menu-997";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_997].m_Function = [&]() {
        return m_aExtraMenuActive[996];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_998].m_pId = "extra-menu-998";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_998].m_Function = [&]() {
        return m_aExtraMenuActive[997];
	};
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_999].m_pId = "extra-menu-999";
        m_aVisibilityFunctions[(int)EButtonVisibility::EXTRA_MENU_999].m_Function = [&]() {
        return m_aExtraMenuActive[998];
	};
}

int CTouchControls::NextActiveAction(int Action) const
{
	switch(Action)
	{
	case ACTION_FIRE:
		return ACTION_HOOK;
	case ACTION_HOOK:
		return ACTION_FIRE;
	default:
		dbg_assert(false, "Action invalid for NextActiveAction");
		return NUM_ACTIONS;
	}
}

int CTouchControls::NextDirectTouchAction() const
{
	if(m_pClient->m_Snap.m_SpecInfo.m_Active)
	{
		switch(m_DirectTouchSpectate)
		{
		case EDirectTouchSpectateMode::DISABLED:
			return NUM_ACTIONS;
		case EDirectTouchSpectateMode::AIM:
			return ACTION_AIM;
		default:
			dbg_assert(false, "m_DirectTouchSpectate invalid");
			return NUM_ACTIONS;
		}
	}
	else
	{
		switch(m_DirectTouchIngame)
		{
		case EDirectTouchIngameMode::DISABLED:
			return NUM_ACTIONS;
		case EDirectTouchIngameMode::ACTION:
			return m_ActionSelected;
		case EDirectTouchIngameMode::AIM:
			return ACTION_AIM;
		case EDirectTouchIngameMode::FIRE:
			return ACTION_FIRE;
		case EDirectTouchIngameMode::HOOK:
			return ACTION_HOOK;
		default:
			dbg_assert(false, "m_DirectTouchIngame invalid");
			return NUM_ACTIONS;
		}
	}
}

void CTouchControls::UpdateButtons(const std::vector<IInput::CTouchFingerState> &vTouchFingerStates)
{
	// Update cached button visibilities and store time that buttons become visible.
	for(CTouchButton &TouchButton : m_vTouchButtons)
	{
		TouchButton.UpdateVisibility();
	}

	const int DirectTouchAction = NextDirectTouchAction();
	const vec2 ScreenSize = CalculateScreenSize();

	std::vector<IInput::CTouchFingerState> vRemainingTouchFingerStates = vTouchFingerStates;

	// Remove remaining finger states for fingers which are responsible for active actions
	// and release action when the finger responsible for it is not pressed down anymore.
	bool GotDirectFingerState = false; // Whether DirectFingerState is valid
	IInput::CTouchFingerState DirectFingerState{}; // The finger that will be used to update the mouse position
	for(int Action = ACTION_AIM; Action < NUM_ACTIONS; ++Action)
	{
		if(!m_aDirectTouchActionStates[Action].m_Active)
		{
			continue;
		}

		const auto ActiveFinger = std::find_if(vRemainingTouchFingerStates.begin(), vRemainingTouchFingerStates.end(), [&](const IInput::CTouchFingerState &TouchFingerState) {
			return TouchFingerState.m_Finger == m_aDirectTouchActionStates[Action].m_Finger;
		});
		if(ActiveFinger == vRemainingTouchFingerStates.end() || DirectTouchAction == NUM_ACTIONS)
		{
			m_aDirectTouchActionStates[Action].m_Active = false;
			if(Action != ACTION_AIM)
			{
				Console()->ExecuteLineStroked(0, ACTION_COMMANDS[Action]);
			}
		}
		else
		{
			if(Action == m_DirectTouchLastAction)
			{
				GotDirectFingerState = true;
				DirectFingerState = *ActiveFinger;
			}
			vRemainingTouchFingerStates.erase(ActiveFinger);
		}
	}

	// Update touch button states after the active action fingers were removed from the vector
	// so that current cursor movement can cross over touch buttons without activating them.

	// Activate visible, inactive buttons with hovered finger. Deactivate previous button being
	// activated by the same finger. Touch buttons are only activated if they became visible
	// before the respective touch finger was pressed down, to prevent repeatedly activating
	// overlapping buttons of excluding visibilities.
	for(CTouchButton &TouchButton : m_vTouchButtons)
	{
		if(!TouchButton.IsVisible() || TouchButton.m_pBehavior->IsActive())
		{
			continue;
		}
		const auto FingerInsideButton = std::find_if(vRemainingTouchFingerStates.begin(), vRemainingTouchFingerStates.end(), [&](const IInput::CTouchFingerState &TouchFingerState) {
			return TouchButton.m_VisibilityStartTime < TouchFingerState.m_PressTime &&
			       TouchButton.IsInside(TouchFingerState.m_Position * ScreenSize);
		});
		if(FingerInsideButton == vRemainingTouchFingerStates.end())
		{
			continue;
		}
		const auto OtherHoveredTouchButton = std::find_if(m_vTouchButtons.begin(), m_vTouchButtons.end(), [&](const CTouchButton &Button) {
			return &Button != &TouchButton && Button.IsVisible() && Button.IsInside(FingerInsideButton->m_Position * ScreenSize);
		});
		if(OtherHoveredTouchButton != m_vTouchButtons.end())
		{
			// Do not activate any button if multiple overlapping buttons are hovered.
			// TODO: Prevent overlapping buttons entirely when parsing the button configuration?
			vRemainingTouchFingerStates.erase(FingerInsideButton);
			continue;
		}
		auto PrevActiveTouchButton = std::find_if(m_vTouchButtons.begin(), m_vTouchButtons.end(), [&](const CTouchButton &Button) {
			return Button.m_pBehavior->IsActive(FingerInsideButton->m_Finger);
		});
		if(PrevActiveTouchButton != m_vTouchButtons.end())
		{
			PrevActiveTouchButton->m_pBehavior->SetInactive();
		}
		TouchButton.m_pBehavior->SetActive(*FingerInsideButton);
	}

	// Deactivate touch buttons only when the respective finger is released, so touch buttons
	// are kept active also if the finger is moved outside the button.
	for(CTouchButton &TouchButton : m_vTouchButtons)
	{
		if(!TouchButton.IsVisible())
		{
			TouchButton.m_pBehavior->SetInactive();
			continue;
		}
		if(!TouchButton.m_pBehavior->IsActive())
		{
			continue;
		}
		const auto ActiveFinger = std::find_if(vRemainingTouchFingerStates.begin(), vRemainingTouchFingerStates.end(), [&](const IInput::CTouchFingerState &TouchFingerState) {
			return TouchFingerState.m_Finger == TouchButton.m_pBehavior->m_Finger;
		});
		if(ActiveFinger == vRemainingTouchFingerStates.end())
		{
			TouchButton.m_pBehavior->SetInactive();
		}
		else
		{
			// Update the already active touch button with the current finger state
			TouchButton.m_pBehavior->SetActive(*ActiveFinger);
		}
	}

	// Remove remaining fingers for active buttons after updating the buttons.
	for(CTouchButton &TouchButton : m_vTouchButtons)
	{
		if(!TouchButton.m_pBehavior->IsActive())
		{
			continue;
		}
		const auto ActiveFinger = std::find_if(vRemainingTouchFingerStates.begin(), vRemainingTouchFingerStates.end(), [&](const IInput::CTouchFingerState &TouchFingerState) {
			return TouchFingerState.m_Finger == TouchButton.m_pBehavior->m_Finger;
		});
		dbg_assert(ActiveFinger != vRemainingTouchFingerStates.end(), "Active button finger not found");
		vRemainingTouchFingerStates.erase(ActiveFinger);
	}

	// TODO: Support standard gesture to zoom (enabled separately for ingame and spectator)

	// Activate action if there is an unhandled pressed down finger.
	int ActivateAction = NUM_ACTIONS;
	if(DirectTouchAction != NUM_ACTIONS && !vRemainingTouchFingerStates.empty() && !m_aDirectTouchActionStates[DirectTouchAction].m_Active)
	{
		GotDirectFingerState = true;
		DirectFingerState = vRemainingTouchFingerStates[0];
		vRemainingTouchFingerStates.erase(vRemainingTouchFingerStates.begin());
		m_aDirectTouchActionStates[DirectTouchAction].m_Active = true;
		m_aDirectTouchActionStates[DirectTouchAction].m_Finger = DirectFingerState.m_Finger;
		m_DirectTouchLastAction = DirectTouchAction;
		ActivateAction = DirectTouchAction;
	}

	// Update mouse position based on the finger responsible for the last active action.
	if(GotDirectFingerState)
	{
		const float Zoom = m_pClient->m_Snap.m_SpecInfo.m_Active ? m_pClient->m_Camera.m_Zoom : 1.0f;
		vec2 WorldScreenSize;
		RenderTools()->CalcScreenParams(Graphics()->ScreenAspect(), Zoom, &WorldScreenSize.x, &WorldScreenSize.y);
		CControls &Controls = GameClient()->m_Controls;
		if(m_pClient->m_Snap.m_SpecInfo.m_Active)
		{
			Controls.m_aMousePos[g_Config.m_ClDummy] += -DirectFingerState.m_Delta * WorldScreenSize;
			Controls.m_aMousePos[g_Config.m_ClDummy].x = clamp(Controls.m_aMousePos[g_Config.m_ClDummy].x, -201.0f * 32, (Collision()->GetWidth() + 201.0f) * 32.0f);
			Controls.m_aMousePos[g_Config.m_ClDummy].y = clamp(Controls.m_aMousePos[g_Config.m_ClDummy].y, -201.0f * 32, (Collision()->GetHeight() + 201.0f) * 32.0f);
		}
		else
		{
			Controls.m_aMousePos[g_Config.m_ClDummy] = (DirectFingerState.m_Position - vec2(0.5f, 0.5f)) * WorldScreenSize;
		}
	}

	// Activate action after the mouse position is set.
	if(ActivateAction != ACTION_AIM && ActivateAction != NUM_ACTIONS)
	{
		Console()->ExecuteLineStroked(1, ACTION_COMMANDS[ActivateAction]);
	}
}

void CTouchControls::ResetButtons()
{
	for(CTouchButton &TouchButton : m_vTouchButtons)
	{
		TouchButton.m_pBehavior->Reset();
	}
	for(CActionState &ActionState : m_aDirectTouchActionStates)
	{
		ActionState.m_Active = false;
	}
}

void CTouchControls::RenderButtons()
{
	for(CTouchButton &TouchButton : m_vTouchButtons)
	{
		TouchButton.UpdateVisibility();
		if(!TouchButton.IsVisible())
		{
			continue;
		}
		TouchButton.Render();
	}
}

vec2 CTouchControls::CalculateScreenSize() const
{
	const float ScreenHeight = 400.0f * 3.0f;
	const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
	return vec2(ScreenWidth, ScreenHeight);
}

bool CTouchControls::ParseConfiguration(const void *pFileData, unsigned FileLength)
{
	json_settings JsonSettings{};
	char aError[256];
	json_value *pConfiguration = json_parse_ex(&JsonSettings, static_cast<const json_char *>(pFileData), FileLength, aError);

	if(pConfiguration == nullptr)
	{
		log_error("touch_controls", "Failed to parse configuration (invalid json): '%s'", aError);
		return false;
	}
	if(pConfiguration->type != json_object)
	{
		log_error("touch_controls", "Failed to parse configuration: root must be an object");
		json_value_free(pConfiguration);
		return false;
	}

	std::optional<EDirectTouchIngameMode> ParsedDirectTouchIngame = ParseDirectTouchIngameMode(&(*pConfiguration)["direct-touch-ingame"]);
	if(!ParsedDirectTouchIngame.has_value())
	{
		json_value_free(pConfiguration);
		return false;
	}

	std::optional<EDirectTouchSpectateMode> ParsedDirectTouchSpectate = ParseDirectTouchSpectateMode(&(*pConfiguration)["direct-touch-spectate"]);
	if(!ParsedDirectTouchSpectate.has_value())
	{
		json_value_free(pConfiguration);
		return false;
	}

	const json_value &TouchButtons = (*pConfiguration)["touch-buttons"];
	if(TouchButtons.type != json_array)
	{
		log_error("touch_controls", "Failed to parse configuration: attribute 'touch-buttons' must specify an array");
		json_value_free(pConfiguration);
		return false;
	}

	std::vector<CTouchButton> vParsedTouchButtons;
	vParsedTouchButtons.reserve(TouchButtons.u.array.length);
	for(unsigned ButtonIndex = 0; ButtonIndex < TouchButtons.u.array.length; ++ButtonIndex)
	{
		std::optional<CTouchButton> ParsedButton = ParseButton(&TouchButtons[ButtonIndex]);
		if(!ParsedButton.has_value())
		{
			log_error("touch_controls", "Failed to parse configuration: could not parse button at index '%d'", ButtonIndex);
			json_value_free(pConfiguration);
			return false;
		}

		vParsedTouchButtons.push_back(std::move(ParsedButton.value()));
	}

	// Parsing successful. Apply parsed configuration.
	m_DirectTouchIngame = ParsedDirectTouchIngame.value();
	m_DirectTouchSpectate = ParsedDirectTouchSpectate.value();

	m_pPrimaryJoystickTouchButtonBehavior = nullptr;
	m_vTouchButtons = std::move(vParsedTouchButtons);
	for(CTouchButton &TouchButton : m_vTouchButtons)
	{
		TouchButton.UpdatePointers();
		TouchButton.UpdateScreenFromUnitRect();
		TouchButton.UpdateBackgroundCorners();
	}

	json_value_free(pConfiguration);

	return true;
}

std::optional<CTouchControls::EDirectTouchIngameMode> CTouchControls::ParseDirectTouchIngameMode(const json_value *pModeValue)
{
	// TODO: Remove json_boolean backwards compatibility
	const json_value &DirectTouchIngame = *pModeValue;
	if(DirectTouchIngame.type != json_boolean && DirectTouchIngame.type != json_string)
	{
		log_error("touch_controls", "Failed to parse configuration: attribute 'direct-touch-ingame' must specify a string");
		return {};
	}
	if(DirectTouchIngame.type == json_boolean)
	{
		return DirectTouchIngame.u.boolean ? EDirectTouchIngameMode::ACTION : EDirectTouchIngameMode::DISABLED;
	}
	EDirectTouchIngameMode ParsedDirectTouchIngame = EDirectTouchIngameMode::NUM_STATES;
	for(int CurrentMode = (int)EDirectTouchIngameMode::DISABLED; CurrentMode < (int)EDirectTouchIngameMode::NUM_STATES; ++CurrentMode)
	{
		if(str_comp(DirectTouchIngame.u.string.ptr, DIRECT_TOUCH_INGAME_MODE_NAMES[CurrentMode]) == 0)
		{
			ParsedDirectTouchIngame = (EDirectTouchIngameMode)CurrentMode;
			break;
		}
	}
	if(ParsedDirectTouchIngame == EDirectTouchIngameMode::NUM_STATES)
	{
		log_error("touch_controls", "Failed to parse configuration: attribute 'direct-touch-ingame' specifies unknown value '%s'", DirectTouchIngame.u.string.ptr);
		return {};
	}
	return ParsedDirectTouchIngame;
}

std::optional<CTouchControls::EDirectTouchSpectateMode> CTouchControls::ParseDirectTouchSpectateMode(const json_value *pModeValue)
{
	// TODO: Remove json_boolean backwards compatibility
	const json_value &DirectTouchSpectate = *pModeValue;
	if(DirectTouchSpectate.type != json_boolean && DirectTouchSpectate.type != json_string)
	{
		log_error("touch_controls", "Failed to parse configuration: attribute 'direct-touch-spectate' must specify a string");
		return {};
	}
	if(DirectTouchSpectate.type == json_boolean)
	{
		return DirectTouchSpectate.u.boolean ? EDirectTouchSpectateMode::AIM : EDirectTouchSpectateMode::DISABLED;
	}
	EDirectTouchSpectateMode ParsedDirectTouchSpectate = EDirectTouchSpectateMode::NUM_STATES;
	for(int CurrentMode = (int)EDirectTouchSpectateMode::DISABLED; CurrentMode < (int)EDirectTouchSpectateMode::NUM_STATES; ++CurrentMode)
	{
		if(str_comp(DirectTouchSpectate.u.string.ptr, DIRECT_TOUCH_SPECTATE_MODE_NAMES[CurrentMode]) == 0)
		{
			ParsedDirectTouchSpectate = (EDirectTouchSpectateMode)CurrentMode;
			break;
		}
	}
	if(ParsedDirectTouchSpectate == EDirectTouchSpectateMode::NUM_STATES)
	{
		log_error("touch_controls", "Failed to parse configuration: attribute 'direct-touch-spectate' specifies unknown value '%s'", DirectTouchSpectate.u.string.ptr);
		return {};
	}
	return ParsedDirectTouchSpectate;
}

std::optional<CTouchControls::CTouchButton> CTouchControls::ParseButton(const json_value *pButtonObject)
{
	const json_value &ButtonObject = *pButtonObject;
	if(ButtonObject.type != json_object)
	{
		log_error("touch_controls", "Failed to parse touch button: must be an object");
		return {};
	}

	const auto &&ParsePositionSize = [&](const char *pAttribute, int &ParsedValue, int Min, int Max) {
		const json_value &AttributeValue = ButtonObject[pAttribute];
		if(AttributeValue.type != json_integer || !in_range<json_int_t>(AttributeValue.u.integer, Min, Max))
		{
			log_error("touch_controls", "Failed to parse touch button: attribute '%s' must specify an integer between '%d' and '%d'", pAttribute, Min, Max);
			return false;
		}
		ParsedValue = AttributeValue.u.integer;
		return true;
	};
	CUnitRect ParsedUnitRect;
	if(!ParsePositionSize("w", ParsedUnitRect.m_W, BUTTON_SIZE_MINIMUM, BUTTON_SIZE_MAXIMUM) ||
		!ParsePositionSize("h", ParsedUnitRect.m_H, BUTTON_SIZE_MINIMUM, BUTTON_SIZE_MAXIMUM))
	{
		return {};
	}
	if(!ParsePositionSize("x", ParsedUnitRect.m_X, 0, BUTTON_SIZE_SCALE - ParsedUnitRect.m_W) ||
		!ParsePositionSize("y", ParsedUnitRect.m_Y, 0, BUTTON_SIZE_SCALE - ParsedUnitRect.m_H))
	{
		return {};
	}

	const json_value &Shape = ButtonObject["shape"];
	if(Shape.type != json_string)
	{
		log_error("touch_controls", "Failed to parse touch button: attribute 'shape' must specify a string");
		return {};
	}
	EButtonShape ParsedShape = EButtonShape::NUM_SHAPES;
	for(int CurrentShape = (int)EButtonShape::RECT; CurrentShape < (int)EButtonShape::NUM_SHAPES; ++CurrentShape)
	{
		if(str_comp(Shape.u.string.ptr, SHAPE_NAMES[CurrentShape]) == 0)
		{
			ParsedShape = (EButtonShape)CurrentShape;
			break;
		}
	}
	if(ParsedShape == EButtonShape::NUM_SHAPES)
	{
		log_error("touch_controls", "Failed to parse touch button: attribute 'shape' specifies unknown value '%s'", Shape.u.string.ptr);
		return {};
	}

	const json_value &Visibilities = ButtonObject["visibilities"];
	if(Visibilities.type != json_array)
	{
		log_error("touch_controls", "Failed to parse touch button: attribute 'visibilities' must specify an array");
		return {};
	}
	std::vector<CButtonVisibility> vParsedVisibilities;
	for(unsigned VisibilityIndex = 0; VisibilityIndex < Visibilities.u.array.length; ++VisibilityIndex)
	{
		const json_value &Visibility = Visibilities[VisibilityIndex];
		if(Visibility.type != json_string)
		{
			log_error("touch_controls", "Failed to parse touch button: attribute 'visibilities' does not specify string at index '%d'", VisibilityIndex);
			return {};
		}
		EButtonVisibility ParsedVisibility = EButtonVisibility::NUM_VISIBILITIES;
		const bool ParsedParity = Visibility.u.string.ptr[0] != '-';
		const char *pVisibilityString = ParsedParity ? Visibility.u.string.ptr : &Visibility.u.string.ptr[1];
		for(int CurrentVisibility = (int)EButtonVisibility::INGAME; CurrentVisibility < (int)EButtonVisibility::NUM_VISIBILITIES; ++CurrentVisibility)
		{
			if(str_comp(pVisibilityString, m_aVisibilityFunctions[CurrentVisibility].m_pId) == 0)
			{
				ParsedVisibility = (EButtonVisibility)CurrentVisibility;
				break;
			}
		}
		if(ParsedVisibility == EButtonVisibility::NUM_VISIBILITIES)
		{
			log_error("touch_controls", "Failed to parse touch button: attribute 'visibilities' specifies unknown value '%s' at index '%d'", pVisibilityString, VisibilityIndex);
			return {};
		}
		const bool VisibilityAlreadyUsed = std::any_of(vParsedVisibilities.begin(), vParsedVisibilities.end(), [&](CButtonVisibility OtherParsedVisibility) {
			return OtherParsedVisibility.m_Type == ParsedVisibility;
		});
		if(VisibilityAlreadyUsed)
		{
			log_error("touch_controls", "Failed to parse touch button: attribute 'visibilities' specifies duplicate value '%s' at '%d'", pVisibilityString, VisibilityIndex);
			return {};
		}
		vParsedVisibilities.emplace_back(ParsedVisibility, ParsedParity);
	}

	std::unique_ptr<CTouchButtonBehavior> pParsedBehavior = ParseBehavior(&ButtonObject["behavior"]);
	if(pParsedBehavior == nullptr)
	{
		log_error("touch_controls", "Failed to parse touch button: failed to parse attribute 'behavior' (see details above)");
		return {};
	}

	CTouchButton Button(this);
	Button.m_UnitRect = ParsedUnitRect;
	Button.m_Shape = ParsedShape;
	Button.m_vVisibilities = std::move(vParsedVisibilities);
	Button.m_pBehavior = std::move(pParsedBehavior);
	return Button;
}

std::unique_ptr<CTouchControls::CTouchButtonBehavior> CTouchControls::ParseBehavior(const json_value *pBehaviorObject)
{
	const json_value &BehaviorObject = *pBehaviorObject;
	if(BehaviorObject.type != json_object)
	{
		log_error("touch_controls", "Failed to parse touch button behavior: must be an object");
		return nullptr;
	}

	const json_value &BehaviorType = BehaviorObject["type"];
	if(BehaviorType.type != json_string)
	{
		log_error("touch_controls", "Failed to parse touch button behavior: attribute 'type' must specify a string");
		return nullptr;
	}

	if(str_comp(BehaviorType.u.string.ptr, CPredefinedTouchButtonBehavior::BEHAVIOR_TYPE) == 0)
	{
		return ParsePredefinedBehavior(&BehaviorObject);
	}
	else if(str_comp(BehaviorType.u.string.ptr, CBindTouchButtonBehavior::BEHAVIOR_TYPE) == 0)
	{
		return ParseBindBehavior(&BehaviorObject);
	}
	else if(str_comp(BehaviorType.u.string.ptr, CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE) == 0)
	{
		return ParseBindToggleBehavior(&BehaviorObject);
	}
	else
	{
		log_error("touch_controls", "Failed to parse touch button behavior: attribute 'type' specifies unknown value '%s'", BehaviorType.u.string.ptr);
		return nullptr;
	}
}

std::unique_ptr<CTouchControls::CPredefinedTouchButtonBehavior> CTouchControls::ParsePredefinedBehavior(const json_value *pBehaviorObject)
{
	const json_value &BehaviorObject = *pBehaviorObject;
	const json_value &PredefinedId = BehaviorObject["id"];
	if(PredefinedId.type != json_string)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'id' must specify a string", CPredefinedTouchButtonBehavior::BEHAVIOR_TYPE);
		return nullptr;
	}

	class CBehaviorFactory
	{
	public:
		const char *m_pId;
		std::function<std::unique_ptr<CPredefinedTouchButtonBehavior>(const json_value *pBehaviorObject)> m_Factory;
	};
	static const CBehaviorFactory BEHAVIOR_FACTORIES[] = {
		{CIngameMenuTouchButtonBehavior::BEHAVIOR_ID, [](const json_value *pBehavior) { return std::make_unique<CIngameMenuTouchButtonBehavior>(); }},
		{CExtraMenuTouchButtonBehavior::BEHAVIOR_ID, [&](const json_value *pBehavior) { return ParseExtraMenuBehavior(pBehavior); }},
		{CCloseAllExtraMenuTouchButtonBehavior::BEHAVIOR_ID, [](const json_value *pBehavior){ return std::make_unique<CCloseAllExtraMenuTouchButtonBehavior>(); }},
		{CEmoticonTouchButtonBehavior::BEHAVIOR_ID, [](const json_value *pBehavior) { return std::make_unique<CEmoticonTouchButtonBehavior>(); }},
		{CSpectateTouchButtonBehavior::BEHAVIOR_ID, [](const json_value *pBehavior) { return std::make_unique<CSpectateTouchButtonBehavior>(); }},
		{CSwapActionTouchButtonBehavior::BEHAVIOR_ID, [](const json_value *pBehavior) { return std::make_unique<CSwapActionTouchButtonBehavior>(); }},
		{CUseActionTouchButtonBehavior::BEHAVIOR_ID, [](const json_value *pBehavior) { return std::make_unique<CUseActionTouchButtonBehavior>(); }},
		{CJoystickActionTouchButtonBehavior::BEHAVIOR_ID, [](const json_value *pBehavior) { return std::make_unique<CJoystickActionTouchButtonBehavior>(); }},
		{CJoystickAimTouchButtonBehavior::BEHAVIOR_ID, [](const json_value *pBehavior) { return std::make_unique<CJoystickAimTouchButtonBehavior>(); }},
		{CJoystickFireTouchButtonBehavior::BEHAVIOR_ID, [](const json_value *pBehavior) { return std::make_unique<CJoystickFireTouchButtonBehavior>(); }},
		{CJoystickHookTouchButtonBehavior::BEHAVIOR_ID, [](const json_value *pBehavior) { return std::make_unique<CJoystickHookTouchButtonBehavior>(); }}};
	for(const CBehaviorFactory &BehaviorFactory : BEHAVIOR_FACTORIES)
	{
		if(str_comp(PredefinedId.u.string.ptr, BehaviorFactory.m_pId) == 0)
		{
			return BehaviorFactory.m_Factory(&BehaviorObject);
		}
	}

	log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'id' specifies unknown value '%s'", CPredefinedTouchButtonBehavior::BEHAVIOR_TYPE, PredefinedId.u.string.ptr);
	return nullptr;
}

std::unique_ptr<CTouchControls::CExtraMenuTouchButtonBehavior> CTouchControls::ParseExtraMenuBehavior(const json_value *pBehaviorObject)
{
	const json_value &BehaviorObject = *pBehaviorObject;
	const json_value &MenuNumber = BehaviorObject["number"];
	// TODO: Remove json_none backwards compatibility
	const int MaxNumber = (int)EButtonVisibility::EXTRA_MENU_999 - (int)EButtonVisibility::EXTRA_MENU_1 + 1;
	if(MenuNumber.type != json_none && (MenuNumber.type != json_integer || !in_range<json_int_t>(MenuNumber.u.integer, 1, MaxNumber)))
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s' and ID '%s': attribute 'number' must specify an integer between '%d' and '%d'",
			CPredefinedTouchButtonBehavior::BEHAVIOR_TYPE, CExtraMenuTouchButtonBehavior::BEHAVIOR_ID, 1, MaxNumber);
		return nullptr;
	}
	int ParsedMenuNumber = MenuNumber.type == json_none ? 0 : (MenuNumber.u.integer - 1);

	return std::make_unique<CExtraMenuTouchButtonBehavior>(ParsedMenuNumber);
}

std::unique_ptr<CTouchControls::CBindTouchButtonBehavior> CTouchControls::ParseBindBehavior(const json_value *pBehaviorObject)
{
	const json_value &BehaviorObject = *pBehaviorObject;
	const json_value &Label = BehaviorObject["label"];
	if(Label.type != json_string)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'label' must specify a string", CBindTouchButtonBehavior::BEHAVIOR_TYPE);
		return nullptr;
	}

	const json_value &LabelType = BehaviorObject["label-type"];
	if(LabelType.type != json_string)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'label-type' must specify a string", CBindTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	CButtonLabel::EType ParsedLabelType = CButtonLabel::EType::NUM_TYPES;
	for(int CurrentType = (int)CButtonLabel::EType::PLAIN; CurrentType < (int)CButtonLabel::EType::NUM_TYPES; ++CurrentType)
	{
		if(str_comp(LabelType.u.string.ptr, LABEL_TYPE_NAMES[CurrentType]) == 0)
		{
			ParsedLabelType = (CButtonLabel::EType)CurrentType;
			break;
		}
	}
	if(ParsedLabelType == CButtonLabel::EType::NUM_TYPES)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'label-type' specifies unknown value '%s'", CBindTouchButtonBehavior::BEHAVIOR_TYPE, LabelType.u.string.ptr);
		return {};
	}

	const json_value &Command = BehaviorObject["command"];
	if(Command.type != json_string)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'command' must specify a string", CBindTouchButtonBehavior::BEHAVIOR_TYPE);
		return nullptr;
	}

	return std::make_unique<CBindTouchButtonBehavior>(Label.u.string.ptr, ParsedLabelType, Command.u.string.ptr);
}

std::unique_ptr<CTouchControls::CBindToggleTouchButtonBehavior> CTouchControls::ParseBindToggleBehavior(const json_value *pBehaviorObject)
{
	const json_value &CommandsObject = (*pBehaviorObject)["commands"];
	if(CommandsObject.type != json_array || CommandsObject.u.array.length < 2)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'commands' must specify an array with at least 2 entries", CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}

	std::vector<CTouchControls::CBindToggleTouchButtonBehavior::CCommand> vCommands;
	vCommands.reserve(CommandsObject.u.array.length);
	for(unsigned CommandIndex = 0; CommandIndex < CommandsObject.u.array.length; ++CommandIndex)
	{
		const json_value &CommandObject = CommandsObject[CommandIndex];
		if(CommandObject.type != json_object)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'commands' must specify an array of objects", CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return nullptr;
		}

		const json_value &Label = CommandObject["label"];
		if(Label.type != json_string)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'label' must specify a string", CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return nullptr;
		}

		const json_value &LabelType = CommandObject["label-type"];
		if(LabelType.type != json_string)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'label-type' must specify a string", CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return {};
		}
		CButtonLabel::EType ParsedLabelType = CButtonLabel::EType::NUM_TYPES;
		for(int CurrentType = (int)CButtonLabel::EType::PLAIN; CurrentType < (int)CButtonLabel::EType::NUM_TYPES; ++CurrentType)
		{
			if(str_comp(LabelType.u.string.ptr, LABEL_TYPE_NAMES[CurrentType]) == 0)
			{
				ParsedLabelType = (CButtonLabel::EType)CurrentType;
				break;
			}
		}
		if(ParsedLabelType == CButtonLabel::EType::NUM_TYPES)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'label-type' specifies unknown value '%s'", CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex, LabelType.u.string.ptr);
			return {};
		}

		const json_value &Command = CommandObject["command"];
		if(Command.type != json_string)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'command' must specify a string", CBindToggleTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return nullptr;
		}
		vCommands.emplace_back(Label.u.string.ptr, ParsedLabelType, Command.u.string.ptr);
	}
	return std::make_unique<CBindToggleTouchButtonBehavior>(std::move(vCommands));
}

void CTouchControls::WriteConfiguration(CJsonWriter *pWriter)
{
	pWriter->BeginObject();

	pWriter->WriteAttribute("direct-touch-ingame");
	pWriter->WriteStrValue(DIRECT_TOUCH_INGAME_MODE_NAMES[(int)m_DirectTouchIngame]);

	pWriter->WriteAttribute("direct-touch-spectate");
	pWriter->WriteStrValue(DIRECT_TOUCH_SPECTATE_MODE_NAMES[(int)m_DirectTouchSpectate]);

	pWriter->WriteAttribute("touch-buttons");
	pWriter->BeginArray();
	for(CTouchButton &TouchButton : m_vTouchButtons)
	{
		TouchButton.WriteToConfiguration(pWriter);
	}
	pWriter->EndArray();

	pWriter->EndObject();
}
