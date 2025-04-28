#include <base/log.h>

#include <engine/console.h>
#include <engine/external/json-parser/json.h>
#include <engine/shared/jsonwriter.h>

#include <game/client/components/touch_controls.h>
#include <game/client/gameclient.h>

// Stores extra menu predefined behavior.

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

std::unique_ptr<CTouchControls::CExtraMenuTouchButtonBehavior> CTouchControls::ParseExtraMenuBehavior(const json_value *pBehaviorObject)
{
	const json_value &BehaviorObject = *pBehaviorObject;
	const json_value &MenuNumber = BehaviorObject["number"];
	// TODO: Remove json_none backwards compatibility
	if(MenuNumber.type != json_none && (MenuNumber.type != json_integer || !in_range<json_int_t>(MenuNumber.u.integer, 1, MAXNUMBER)))
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s' and ID '%s': attribute 'number' must specify an integer between '%d' and '%d'",
			CPredefinedTouchButtonBehavior::BEHAVIOR_TYPE, CExtraMenuTouchButtonBehavior::BEHAVIOR_ID, 1, MAXNUMBER);
		return nullptr;
	}
	int ParsedMenuNumber = MenuNumber.type == json_none ? 0 : (MenuNumber.u.integer - 1);

	return std::make_unique<CExtraMenuTouchButtonBehavior>(ParsedMenuNumber);
}
