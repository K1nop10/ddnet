#include <base/log.h>

#include <engine/console.h>
#include <engine/external/json-parser/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/localization.h>

#include <game/client/components/touch_controls.h>
#include <game/client/gameclient.h>

// Stores predefined actions that are considered simple enough.

// Ingame menu button: always opens ingame menu.
CTouchControls::CButtonLabel CTouchControls::CIngameMenuTouchButtonBehavior::GetLabel() const
{
	return {CButtonLabel::EType::ICON, "\xEF\x85\x8E"};
}

void CTouchControls::CIngameMenuTouchButtonBehavior::OnDeactivate()
{
	m_pTouchControls->GameClient()->m_Menus.SetActive(true);
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
