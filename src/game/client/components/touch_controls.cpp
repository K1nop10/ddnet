#include "touch_controls.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/external/json-parser/json.h>
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
static constexpr float EPS = 1e-4;

void SuperMap::Init()
{
	Map["cl_button_color_type"] = &g_Config.m_ClButtonColorType;
	Map["cl_button_rainbow_speed"] = &g_Config.m_ClButtonRainbowSpeed;
	Map["cl_button_rainbow_sat"] = &g_Config.m_ClButtonRainbowSat;
	Map["cl_button_rainbow_lig"] = &g_Config.m_ClButtonRainbowLig;
	Map["cl_button_alpha"] = &g_Config.m_ClButtonAlpha;
	Map["cl_button_alpha_active"] = &g_Config.m_ClButtonAlphaActive;
	Map["cl_label_color_type"] = &g_Config.m_ClLabelColorType;
	Map["cl_label_rainbow_speed"] = &g_Config.m_ClLabelRainbowSpeed;
	Map["cl_label_rainbow_sat"] = &g_Config.m_ClLabelRainbowSat;
	Map["cl_label_rainbow_lig"] = &g_Config.m_ClLabelRainbowLig;
	Map["cl_label_alpha"] = &g_Config.m_ClLabelAlpha;
	Map["cl_label_alpha_active"] = &g_Config.m_ClLabelAlphaActive;
	Map["cl_bind_slide_extra_render_time"] = &g_Config.m_ClBindSlideExtraRenderTime;
	Map["cl_bind_slide_distance"] = &g_Config.m_ClBindSlideDistance;
	Map["cl_predict"] = &g_Config.m_ClPredict;
	Map["cl_predict_dummy"] = &g_Config.m_ClPredictDummy;
	Map["cl_antiping_limit"] = &g_Config.m_ClAntiPingLimit;
	Map["cl_antiping"] = &g_Config.m_ClAntiPing;
	Map["cl_antiping_players"] = &g_Config.m_ClAntiPingPlayers;
	Map["cl_antiping_grenade"] = &g_Config.m_ClAntiPingGrenade;
	Map["cl_antiping_weapons"] = &g_Config.m_ClAntiPingWeapons;
	Map["cl_antiping_smooth"] = &g_Config.m_ClAntiPingSmooth;
	Map["cl_antiping_gunfire"] = &g_Config.m_ClAntiPingGunfire;
	Map["cl_prediction_margin"] = &g_Config.m_ClPredictionMargin;
	Map["cl_sub_tick_aiming"] = &g_Config.m_ClSubTickAiming;
	Map["cl_touch_controls"] = &g_Config.m_ClTouchControls;
	Map["cl_touch_controls"] = &g_Config.m_ClTouchControls;
	Map["cl_nameplates"] = &g_Config.m_ClNamePlates;
	Map["cl_nameplates_always"] = &g_Config.m_ClNamePlatesAlways;
	Map["cl_nameplates_teamcolors"] = &g_Config.m_ClNamePlatesTeamcolors;
	Map["cl_nameplates_size"] = &g_Config.m_ClNamePlatesSize;
	Map["cl_nameplates_clan"] = &g_Config.m_ClNamePlatesClan;
	Map["cl_nameplates_clan_size"] = &g_Config.m_ClNamePlatesClanSize;
	Map["cl_nameplates_ids"] = &g_Config.m_ClNamePlatesIds;
	Map["cl_nameplates_own"] = &g_Config.m_ClNamePlatesOwn;
	Map["cl_nameplates_friendmark"] = &g_Config.m_ClNamePlatesFriendMark;
	Map["cl_nameplates_strong"] = &g_Config.m_ClNamePlatesStrong;
	Map["cl_nameplates_strong_size"] = &g_Config.m_ClNamePlatesStrongSize;
	Map["cl_afk_emote"] = &g_Config.m_ClAfkEmote;
	Map["cl_text_entities"] = &g_Config.m_ClTextEntities;
	Map["cl_text_entities_size"] = &g_Config.m_ClTextEntitiesSize;
	Map["cl_streamer_mode"] = &g_Config.m_ClStreamerMode;
	Map["cl_enable_ping_color"] = &g_Config.m_ClEnablePingColor;
	Map["cl_autoswitch_weapons"] = &g_Config.m_ClAutoswitchWeapons;
	Map["cl_autoswitch_weapons_out_of_ammo"] = &g_Config.m_ClAutoswitchWeaponsOutOfAmmo;
	Map["cl_showhud"] = &g_Config.m_ClShowhud;
	Map["cl_showhud_healthammo"] = &g_Config.m_ClShowhudHealthAmmo;
	Map["cl_showhud_score"] = &g_Config.m_ClShowhudScore;
	Map["cl_showhud_timer"] = &g_Config.m_ClShowhudTimer;
	Map["cl_showhud_time_cp_diff"] = &g_Config.m_ClShowhudTimeCpDiff;
	Map["cl_showhud_dummy_actions"] = &g_Config.m_ClShowhudDummyActions;
	Map["cl_showhud_player_position"] = &g_Config.m_ClShowhudPlayerPosition;
	Map["cl_showhud_player_speed"] = &g_Config.m_ClShowhudPlayerSpeed;
	Map["cl_showhud_player_angle"] = &g_Config.m_ClShowhudPlayerAngle;
	Map["cl_showhud_ddrace"] = &g_Config.m_ClShowhudDDRace;
	Map["cl_showhud_jumps_indicator"] = &g_Config.m_ClShowhudJumpsIndicator;
	Map["cl_show_freeze_bars"] = &g_Config.m_ClShowFreezeBars;
	Map["cl_freezebars_alpha_inside_freeze"] = &g_Config.m_ClFreezeBarsAlphaInsideFreeze;
	Map["cl_showrecord"] = &g_Config.m_ClShowRecord;
	Map["cl_shownotifications"] = &g_Config.m_ClShowNotifications;
	Map["cl_showemotes"] = &g_Config.m_ClShowEmotes;
	Map["cl_showchat"] = &g_Config.m_ClShowChat;
	Map["cl_show_chat_friends"] = &g_Config.m_ClShowChatFriends;
	Map["cl_show_chat_team_members_only"] = &g_Config.m_ClShowChatTeamMembersOnly;
	Map["cl_show_chat_system"] = &g_Config.m_ClShowChatSystem;
	Map["cl_showkillmessages"] = &g_Config.m_ClShowKillMessages;
	Map["cl_show_finish_messages"] = &g_Config.m_ClShowFinishMessages;
	Map["cl_show_votes_after_voting"] = &g_Config.m_ClShowVotesAfterVoting;
	Map["cl_show_local_time_always"] = &g_Config.m_ClShowLocalTimeAlways;
	Map["cl_showfps"] = &g_Config.m_ClShowfps;
	Map["cl_showpred"] = &g_Config.m_ClShowpred;
	Map["cl_eye_wheel"] = &g_Config.m_ClEyeWheel;
	Map["cl_eye_duration"] = &g_Config.m_ClEyeDuration;
	Map["cl_freeze_stars"] = &g_Config.m_ClFreezeStars;
	Map["cl_spec_cursor"] = &g_Config.m_ClSpecCursor;
	Map["cl_spec_auto_sync"] = &g_Config.m_ClSpecAutoSync;
	Map["cl_airjumpindicator"] = &g_Config.m_ClAirjumpindicator;
	Map["cl_threadsoundloading"] = &g_Config.m_ClThreadsoundloading;
	Map["cl_warning_teambalance"] = &g_Config.m_ClWarningTeambalance;
	Map["cl_mouse_deadzone"] = &g_Config.m_ClMouseDeadzone;
	Map["cl_mouse_followfactor"] = &g_Config.m_ClMouseFollowfactor;
	Map["cl_mouse_max_distance"] = &g_Config.m_ClMouseMaxDistance;
	Map["cl_mouse_min_distance"] = &g_Config.m_ClMouseMinDistance;
	Map["cl_dyncam"] = &g_Config.m_ClDyncam;
	Map["cl_dyncam_max_distance"] = &g_Config.m_ClDyncamMaxDistance;
	Map["cl_dyncam_min_distance"] = &g_Config.m_ClDyncamMinDistance;
	Map["cl_dyncam_mousesens"] = &g_Config.m_ClDyncamMousesens;
	Map["cl_dyncam_deadzone"] = &g_Config.m_ClDyncamDeadzone;
	Map["cl_dyncam_follow_factor"] = &g_Config.m_ClDyncamFollowFactor;
	Map["cl_dyncam_smoothness"] = &g_Config.m_ClDyncamSmoothness;
	Map["cl_dyncam_stabilizing"] = &g_Config.m_ClDyncamStabilizing;
	Map["cl_multiview_sensitivity"] = &g_Config.m_ClMultiViewSensitivity;
	Map["cl_multiview_zoom_smoothness"] = &g_Config.m_ClMultiViewZoomSmoothness;
	Map["cl_spectator_mouseclicks"] = &g_Config.m_ClSpectatorMouseclicks;
	Map["cl_smooth_spectating_time"] = &g_Config.m_ClSmoothSpectatingTime;
	Map["ed_autosave_interval"] = &g_Config.m_EdAutosaveInterval;
	Map["ed_autosave_max"] = &g_Config.m_EdAutosaveMax;
	Map["ed_smooth_zoom_time"] = &g_Config.m_EdSmoothZoomTime;
	Map["ed_limit_max_zoom_level"] = &g_Config.m_EdLimitMaxZoomLevel;
	Map["ed_zoom_target"] = &g_Config.m_EdZoomTarget;
	Map["ed_showkeys"] = &g_Config.m_EdShowkeys;
	Map["ed_align_quads"] = &g_Config.m_EdAlignQuads;
	Map["ed_show_quads_rect"] = &g_Config.m_EdShowQuadsRect;
	Map["ed_auto_map_reload"] = &g_Config.m_EdAutoMapReload;
	Map["ed_layer_selector"] = &g_Config.m_EdLayerSelector;
	Map["cl_show_welcome"] = &g_Config.m_ClShowWelcome;
	Map["cl_motd_time"] = &g_Config.m_ClMotdTime;
	Map["cl_map_download_connect_timeout_ms"] = &g_Config.m_ClMapDownloadConnectTimeoutMs;
	Map["cl_map_download_low_speed_limit"] = &g_Config.m_ClMapDownloadLowSpeedLimit;
	Map["cl_map_download_low_speed_time"] = &g_Config.m_ClMapDownloadLowSpeedTime;
	Map["cl_vanilla_skins_only"] = &g_Config.m_ClVanillaSkinsOnly;
	Map["cl_download_skins"] = &g_Config.m_ClDownloadSkins;
	Map["cl_download_community_skins"] = &g_Config.m_ClDownloadCommunitySkins;
	Map["cl_auto_statboard_screenshot"] = &g_Config.m_ClAutoStatboardScreenshot;
	Map["cl_auto_statboard_screenshot_max"] = &g_Config.m_ClAutoStatboardScreenshotMax;
	Map["cl_default_zoom"] = &g_Config.m_ClDefaultZoom;
	Map["cl_smooth_zoom_time"] = &g_Config.m_ClSmoothZoomTime;
	Map["cl_limit_max_zoom_level"] = &g_Config.m_ClLimitMaxZoomLevel;
	Map["player_use_custom_color"] = &g_Config.m_ClPlayerUseCustomColor;
	Map["player_default_eyes"] = &g_Config.m_ClPlayerDefaultEyes;
	Map["cl_fat_skins"] = &g_Config.m_ClFatSkins;
	Map["player7_use_custom_color_body"] = &g_Config.m_ClPlayer7UseCustomColorBody;
	Map["player7_use_custom_color_marking"] = &g_Config.m_ClPlayer7UseCustomColorMarking;
	Map["player7_use_custom_color_decoration"] = &g_Config.m_ClPlayer7UseCustomColorDecoration;
	Map["player7_use_custom_color_hands"] = &g_Config.m_ClPlayer7UseCustomColorHands;
	Map["player7_use_custom_color_feet"] = &g_Config.m_ClPlayer7UseCustomColorFeet;
	Map["player7_use_custom_color_eyes"] = &g_Config.m_ClPlayer7UseCustomColorEyes;
	Map["dummy7_use_custom_color_body"] = &g_Config.m_ClDummy7UseCustomColorBody;
	Map["dummy7_use_custom_color_marking"] = &g_Config.m_ClDummy7UseCustomColorMarking;
	Map["dummy7_use_custom_color_decoration"] = &g_Config.m_ClDummy7UseCustomColorDecoration;
	Map["dummy7_use_custom_color_hands"] = &g_Config.m_ClDummy7UseCustomColorHands;
	Map["dummy7_use_custom_color_feet"] = &g_Config.m_ClDummy7UseCustomColorFeet;
	Map["dummy7_use_custom_color_eyes"] = &g_Config.m_ClDummy7UseCustomColorEyes;
	Map["dummy_country"] = &g_Config.m_ClDummyCountry;
	Map["dummy_use_custom_color"] = &g_Config.m_ClDummyUseCustomColor;
	Map["dummy_default_eyes"] = &g_Config.m_ClDummyDefaultEyes;
	Map["cl_dummy"] = &g_Config.m_ClDummy;
	Map["cl_dummy_hammer"] = &g_Config.m_ClDummyHammer;
	Map["cl_dummy_resetonswitch"] = &g_Config.m_ClDummyResetOnSwitch;
	Map["cl_dummy_restore_weapon"] = &g_Config.m_ClDummyRestoreWeapon;
	Map["cl_dummy_copy_moves"] = &g_Config.m_ClDummyCopyMoves;
	Map["cl_dummy_control"] = &g_Config.m_ClDummyControl;
	Map["cl_dummy_jump"] = &g_Config.m_ClDummyJump;
	Map["cl_dummy_fire"] = &g_Config.m_ClDummyFire;
	Map["cl_dummy_hook"] = &g_Config.m_ClDummyHook;
	Map["cl_show_start_menu_images"] = &g_Config.m_ClShowStartMenuImages;
	Map["cl_skip_start_menu"] = &g_Config.m_ClSkipStartMenu;
	Map["cl_video_pausewithdemo"] = &g_Config.m_ClVideoPauseWithDemo;
	Map["cl_video_showhud"] = &g_Config.m_ClVideoShowhud;
	Map["cl_video_showchat"] = &g_Config.m_ClVideoShowChat;
	Map["cl_video_sound_enable"] = &g_Config.m_ClVideoSndEnable;
	Map["cl_video_show_hook_coll_other"] = &g_Config.m_ClVideoShowHookCollOther;
	Map["cl_video_show_direction"] = &g_Config.m_ClVideoShowDirection;
	Map["cl_video_crf"] = &g_Config.m_ClVideoX264Crf;
	Map["cl_video_preset"] = &g_Config.m_ClVideoX264Preset;
	Map["dbg_tuning"] = &g_Config.m_DbgTuning;
	Map["player_country"] = &g_Config.m_PlayerCountry;
	Map["events"] = &g_Config.m_Events;
	Map["logappend"] = &g_Config.m_Logappend;
	Map["loglevel"] = &g_Config.m_Loglevel;
	Map["stdout_output_level"] = &g_Config.m_StdoutOutputLevel;
	Map["console_output_level"] = &g_Config.m_ConsoleOutputLevel;
	Map["console_enable_colors"] = &g_Config.m_ConsoleEnableColors;
	Map["cl_save_settings"] = &g_Config.m_ClSaveSettings;
	Map["cl_refresh_rate"] = &g_Config.m_ClRefreshRate;
	Map["cl_refresh_rate_inactive"] = &g_Config.m_ClRefreshRateInactive;
	Map["cl_editor"] = &g_Config.m_ClEditor;
	Map["cl_editor_dilate"] = &g_Config.m_ClEditorDilate;
	Map["cl_editor_max_history"] = &g_Config.m_ClEditorMaxHistory;
	Map["cl_auto_demo_record"] = &g_Config.m_ClAutoDemoRecord;
	Map["cl_auto_demo_on_connect"] = &g_Config.m_ClAutoDemoOnConnect;
	Map["cl_auto_demo_max"] = &g_Config.m_ClAutoDemoMax;
	Map["cl_auto_screenshot"] = &g_Config.m_ClAutoScreenshot;
	Map["cl_auto_screenshot_max"] = &g_Config.m_ClAutoScreenshotMax;
	Map["cl_auto_csv"] = &g_Config.m_ClAutoCSV;
	Map["cl_auto_csv_max"] = &g_Config.m_ClAutoCSVMax;
	Map["cl_show_broadcasts"] = &g_Config.m_ClShowBroadcasts;
	Map["cl_print_broadcasts"] = &g_Config.m_ClPrintBroadcasts;
	Map["cl_print_motd"] = &g_Config.m_ClPrintMotd;
	Map["cl_friends_ignore_clan"] = &g_Config.m_ClFriendsIgnoreClan;
	Map["inp_mousesens"] = &g_Config.m_InpMousesens;
	Map["inp_translated_keys"] = &g_Config.m_InpTranslatedKeys;
	Map["inp_ignored_modifiers"] = &g_Config.m_InpIgnoredModifiers;
	Map["cl_race_binds_set"] = &g_Config.m_ClDDRaceBindsSet;
	Map["cl_reconnect_timeout"] = &g_Config.m_ClReconnectTimeout;
	Map["cl_reconnect_full"] = &g_Config.m_ClReconnectFull;
	Map["cl_message_friend"] = &g_Config.m_ClMessageFriend;
	Map["cl_show_ids"] = &g_Config.m_ClShowIds;
	Map["cl_scoreboard_on_death"] = &g_Config.m_ClScoreboardOnDeath;
	Map["cl_auto_race_record"] = &g_Config.m_ClAutoRaceRecord;
	Map["cl_replays"] = &g_Config.m_ClReplays;
	Map["cl_replay_length"] = &g_Config.m_ClReplayLength;
	Map["cl_race_record_server_control"] = &g_Config.m_ClRaceRecordServerControl;
	Map["cl_demo_name"] = &g_Config.m_ClDemoName;
	Map["cl_race_ghost"] = &g_Config.m_ClRaceGhost;
	Map["cl_race_ghost_server_control"] = &g_Config.m_ClRaceGhostServerControl;
	Map["cl_race_show_ghost"] = &g_Config.m_ClRaceShowGhost;
	Map["cl_race_save_ghost"] = &g_Config.m_ClRaceSaveGhost;
	Map["cl_race_ghost_strict_map"] = &g_Config.m_ClRaceGhostStrictMap;
	Map["cl_race_ghost_save_best"] = &g_Config.m_ClRaceGhostSaveBest;
	Map["cl_race_ghost_alpha"] = &g_Config.m_ClRaceGhostAlpha;
	Map["cl_show_others"] = &g_Config.m_ClShowOthers;
	Map["cl_show_others_alpha"] = &g_Config.m_ClShowOthersAlpha;
	Map["cl_overlay_entities"] = &g_Config.m_ClOverlayEntities;
	Map["cl_showquads"] = &g_Config.m_ClShowQuads;
	Map["cl_rotation_radius"] = &g_Config.m_ClRotationRadius;
	Map["cl_rotation_speed"] = &g_Config.m_ClRotationSpeed;
	Map["cl_camera_speed"] = &g_Config.m_ClCameraSpeed;
	Map["cl_background_show_tiles_layers"] = &g_Config.m_ClBackgroundShowTilesLayers;
	Map["cl_unpredicted_shadow"] = &g_Config.m_ClUnpredictedShadow;
	Map["cl_predict_freeze"] = &g_Config.m_ClPredictFreeze;
	Map["cl_show_ninja"] = &g_Config.m_ClShowNinja;
	Map["cl_show_hook_coll_other"] = &g_Config.m_ClShowHookCollOther;
	Map["cl_show_hook_coll_own"] = &g_Config.m_ClShowHookCollOwn;
	Map["cl_hook_coll_size"] = &g_Config.m_ClHookCollSize;
	Map["cl_hook_coll_size_other"] = &g_Config.m_ClHookCollSizeOther;
	Map["cl_hook_coll_alpha"] = &g_Config.m_ClHookCollAlpha;
	Map["cl_chat_teamcolors"] = &g_Config.m_ClChatTeamColors;
	Map["cl_chat_reset"] = &g_Config.m_ClChatReset;
	Map["cl_chat_old"] = &g_Config.m_ClChatOld;
	Map["cl_chat_size"] = &g_Config.m_ClChatFontSize;
	Map["cl_chat_width"] = &g_Config.m_ClChatWidth;
	Map["cl_show_direction"] = &g_Config.m_ClShowDirection;
	Map["cl_direction_size"] = &g_Config.m_ClDirectionSize;
	Map["cl_old_gun_position"] = &g_Config.m_ClOldGunPosition;
	Map["cl_confirm_disconnect_time"] = &g_Config.m_ClConfirmDisconnectTime;
	Map["cl_confirm_quit_time"] = &g_Config.m_ClConfirmQuitTime;
	Map["cl_config_version"] = &g_Config.m_ClConfigVersion;
	Map["cl_demo_slice_begin"] = &g_Config.m_ClDemoSliceBegin;
	Map["cl_demo_slice_end"] = &g_Config.m_ClDemoSliceEnd;
	Map["cl_demo_show_speed"] = &g_Config.m_ClDemoShowSpeed;
	Map["cl_demo_show_pause"] = &g_Config.m_ClDemoShowPause;
	Map["cl_demo_keyboard_shortcuts"] = &g_Config.m_ClDemoKeyboardShortcuts;
}
int* SuperMap::Get(std::string a)
{
	return Map[a];
}

/* This is required for the localization script to find the labels of the default bind buttons specified in the configuration file:
Localizable("Move left") Localizable("Move right") Localizable("Jump") Localizable("Prev. weapon") Localizable("Next weapon")
Localizable("Zoom out") Localizable("Default zoom") Localizable("Zoom in") Localizable("Scoreboard") Localizable("Chat") Localizable("Team chat")
Localizable("Vote yes") Localizable("Vote no") Localizable("Toggle dummy")
*/
//c is target, ab is line.
std::unordered_map<std::string, int*> SuperMap::Map;
auto Cross = [](vec2 a, vec2 b){
	return a.x*b.y-b.x*a.y;
};

auto IsUpLine = [](vec2 c, vec2 a, vec2 b){
	if(minimum(a.x, b.x) <= c.x && maximum(a.x, b.x) >= c.x && maximum(a.y, b.y) >= c.y)
	{
		float k = (b.y - a.y) / (b.x - a.x);
		float d = (a.y * b.x - a.x * b.y) / (b.x - a.x);
		if(c.y - k * c.x - d <= EPS)
			return true;
	}
	return false;
};

auto IsTwoLine = [](vec2 a, vec2 b, vec2 c, vec2 d){
	vec2 ab, cd, ac, ad, cb, ca;
	ab.x=b.x-a.x;cd.x=d.x-c.x;ac.x=c.x-a.x;ad.x=d.x-a.x;cb.x=b.x-c.x;ca.x=a.x-c.x;
	ab.y=b.y-a.y;cd.y=d.y-c.y;ac.y=c.y-a.y;ad.y=d.y-a.y;cb.y=b.y-c.y;ca.y=a.y-c.y;
	if(std::abs(Cross(cd,ab)) <= EPS)
		return false;
	if(Cross(ac,ab)*Cross(ad,ab)<EPS&&Cross(ca,cd)*Cross(cb,cd)<EPS)
		return true;
	return false;
};

auto TwoLine = [](vec2 a, vec2 b, vec2 c, vec2 d)->vec2{
	float t=((b.y-a.y)*(c.x-a.x)-(c.y-a.y)*(b.x-a.x))/((d.y-c.y)*(b.x-a.x)-(b.y-a.y)*(d.x-c.x));
	vec2 gg;
	gg.x=(d.x-c.x)*t+c.x;
	gg.y=(d.y-c.y)*t+c.y;
	return gg;
};

auto Inside = [](float a, float b, vec2 d, vec2 bf){
	float c=2.0f+2*std::sin((18.0f)/360*2*pi);
	vec2 f,j,o,h,e,p,q,u,r,s;
	f.x=a/2;f.y=0.0f;j.x=0.0f;j.y=b/c;o.x=a;o.y=b/c;h.x=a/2/c;h.y=b;e.x=a-a/2/c;e.y=b;
	p.x=a/c;p.y=b/c;q.x=a-a/c;q.y=b/c;u.x=a/c-a/2/c/c;u.y=b-b/c;r.x=a-a/c+a/2/c/c;r.y=b-b/c;s.x=a/2;s.y=b-b/c+b/c/c;
	f+=bf;j+=bf;o+=bf;h+=bf;e+=bf;p+=bf;q+=bf;u+=bf;r+=bf;s+=bf;
	vec2 pts[10]={j,p,f,q,o,r,e,s,h,u};
	int flag=0;
	for(int i=0;i<10;i++)
	{
		if(IsUpLine(d,pts[i],pts[i==9?0:i+1]))
			flag++;
	}
	if(flag%2==0)
		return false;
	return true;
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
	m_vMenus(Other.m_vMenus),
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
	m_vMenus = std::move(Other.m_vMenus);
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
	if(m_Shape == EButtonShape::SSTAR)
	{
		float a=m_ScreenRect.w;
		m_ScreenRect.h=a*std::cos((18.0f)/360*2*pi);
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
		if(&OtherButton == this || OtherButton.m_Shape != EButtonShape::RECT || !OtherButton.IsVisible())
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
	case EButtonShape::STAR:
	case EButtonShape::SSTAR:
	{
		vec2 bf;bf.x=m_ScreenRect.x;bf.y=m_ScreenRect.y;
		if(Inside(m_ScreenRect.w,m_ScreenRect.h,TouchPosition,bf))
			break;
		else
		{
			float a=m_ScreenRect.w;
			float b=m_ScreenRect.h;
			float c=2.0f+2*std::sin((18.0f)/360*2*pi);
			vec2 f,j,o,h,e,p,q,u,r,s;
			f.x=a/2;f.y=0.0f;j.x=0.0f;j.y=b/c;o.x=a;o.y=b/c;h.x=a/2/c;h.y=b;e.x=a-a/2/c;e.y=b;
			p.x=a/c;p.y=b/c;q.x=a-a/c;q.y=b/c;u.x=a/c-a/2/c/c;u.y=b-b/c;r.x=a-a/c+a/2/c/c;r.y=b-b/c;s.x=a/2;s.y=b-b/c+b/c/c;
			vec2 pts[10]={j,p,f,q,o,r,e,s,h,u};
			for(int i=0;i<10;i++){
				pts[i].x+=m_ScreenRect.x;
				pts[i].y+=m_ScreenRect.y;
			}
			int i;
			for(i=0;i<10;i++)
			{
				pts[i].x+=m_ScreenRect.x;
				pts[i].y+=m_ScreenRect.y;
				if(IsTwoLine(TouchPosition,m_ScreenRect.Center(),pts[i],pts[i==9?0:i+1]))
					break;
			}
			TouchPosition=TwoLine(TouchPosition,m_ScreenRect.Center(),pts[i],pts[i==9?0:i+1]);
			break;
		}
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
	case EButtonShape::STAR:
	case EButtonShape::SSTAR:
		{vec2 bf;bf.x=m_ScreenRect.x;bf.y=m_ScreenRect.y;
		return Inside(m_ScreenRect.w,m_ScreenRect.h,TouchPosition,bf);}
	default:
		dbg_assert(false, "Unhandled shape");
		return false;
	}
}

void CTouchControls::CTouchButton::UpdateVisibility()
{
	const bool PrevVisibility = m_VisibilityCached;
	m_VisibilityCached = m_pTouchControls->m_EditingActive || (std::all_of(m_vVisibilities.begin(), m_vVisibilities.end(), [&](CButtonVisibility Visibility) {
		return m_pTouchControls->m_aVisibilityFunctions[(int)Visibility.m_Type].m_Function() == Visibility.m_Parity;
	}) && std::all_of(m_vMenus.begin(), m_vMenus.end(), [&](const auto& Menu) {
		return m_pTouchControls->m_vMenuMap[Menu.first] == Menu.second;
	}));
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
	CBarTouchButtonBehavior* tmp = nullptr;
	tmp = dynamic_cast<CBarTouchButtonBehavior*>(m_pBehavior.get());
	if(tmp)
	{
		m_pTouchControls->Ui()->DoScrollbarOption(tmp->m_Target, tmp->m_Target, &m_ScreenRect, tmp->m_Label.c_str(), tmp->m_Min, tmp->m_Max, &CUi::ms_LinearScrollbarScale, *(tmp->m_Target), "");
		return;
	}
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
	case EButtonShape::STAR:
	case EButtonShape::SSTAR:
	{
		float a=m_ScreenRect.w;
		float b=m_ScreenRect.h;
		float c=2.0f+2*std::sin((18.0f)/360*2*pi);
		vec2 f,j,o,h,e,p,q,u,r,s;
		f.x=a/2;f.y=0.0f;j.x=0.0f;j.y=b/c;o.x=a;o.y=b/c;h.x=a/2/c;h.y=b;e.x=a-a/2/c;e.y=b;
		p.x=a/c;p.y=b/c;q.x=a-a/c;q.y=b/c;u.x=a/c-a/2/c/c;u.y=b-b/c;r.x=a-a/c+a/2/c/c;r.y=b-b/c;s.x=a/2;s.y=b-b/c+b/c/c;
		vec2 bf;bf.x=m_ScreenRect.x;bf.y=m_ScreenRect.y;
		f+=bf;j+=bf;o+=bf;h+=bf;e+=bf;p+=bf;q+=bf;u+=bf;r+=bf;s+=bf;
		IGraphics::CFreeformItem star[5];
		const vec2 Center = m_ScreenRect.Center();
		star[0]=IGraphics::CFreeformItem(Center.x,Center.y,p.x,p.y,q.x,q.y,f.x,f.y);
		star[1]=IGraphics::CFreeformItem(Center.x,Center.y,q.x,q.y,r.x,r.y,o.x,o.y);
		star[2]=IGraphics::CFreeformItem(Center.x,Center.y,r.x,r.y,s.x,s.y,e.x,e.y);
		star[3]=IGraphics::CFreeformItem(Center.x,Center.y,s.x,s.y,u.x,u.y,h.x,h.y);
		star[4]=IGraphics::CFreeformItem(Center.x,Center.y,u.x,u.y,p.x,p.y,j.x,j.y);
		m_pTouchControls->Graphics()->TextureClear();
		m_pTouchControls->Graphics()->QuadsBegin();
		m_pTouchControls->Graphics()->SetColor(ColorRGBA(ButtonColor.r,ButtonColor.g,ButtonColor.b,alpha));
		m_pTouchControls->Graphics()->QuadsDrawFreeform(star,5);
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
	
	CBindSlideTouchButtonBehavior* pTmp = nullptr;
	if(m_pBehavior->GetType() == "bind-slide")
		pTmp = static_cast<CBindSlideTouchButtonBehavior*>(m_pBehavior.get());
	
	if(pTmp && (pTmp->m_IsOpen || pTmp->m_IsSliding))
	{
		size_t RenderFlag = 0;
		if(m_UnitRect.m_X + m_UnitRect.m_W <= 940000)
			RenderFlag += (1 << 0);//1:Right
		if(m_UnitRect.m_X >= 60000)
			RenderFlag += (1 << 1);//2:Left
		if(m_UnitRect.m_Y + m_UnitRect.m_H <= 940000)
			RenderFlag += (1 << 2);//4:Down
		if(m_UnitRect.m_Y >= 60000)
			RenderFlag += (1 << 3);//8:Up
		size_t TrueFlag = 0;
		vec2 Heart;
		Heart.x = m_UnitRect.m_X + m_UnitRect.m_W / 2;
		Heart.y = m_UnitRect.m_Y + m_UnitRect.m_H / 2;
		CUIRect ExtraRect;
		const vec2 ScreenSize = m_pTouchControls->CalculateScreenSize();
		float Dx = ScreenSize.x / BUTTON_SIZE_SCALE;
		float Dy = ScreenSize.y / BUTTON_SIZE_SCALE;
		ExtraRect.w = 50000 * Dx;
		ExtraRect.h = 50000 * Dy;
		float Tan =(pTmp->m_AccumulatedDelta.x != 0) ? pTmp->m_AccumulatedDelta.y / pTmp->m_AccumulatedDelta.x : 100000.0f;
		float rad = pi / 180.0f;
		SLabelProperties ExtraLabelProps = LabelProps;
		ExtraLabelProps.m_MaxWidth = ExtraRect.w;
		float ExtraFontSize = 17.0f;
		vec2 delta;
		delta.x = pTmp->m_AccumulatedDelta.x;
		delta.y = pTmp->m_AccumulatedDelta.y;
		
		if(pTmp->m_AccumulatedDelta.y < 0.0f)
		{
			if(Tan >= std::tan(-22.5 * rad) && Tan < 0 && (RenderFlag & 1) == 1)
			{
				TrueFlag = 1;
				ExtraRect.x = (m_UnitRect.m_X + m_UnitRect.m_W + 10000) * Dx;
				ExtraRect.y = (Heart.y - 25000) * Dy;
				ExtraRect.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
				m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::RIGHT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
			}
			if(Tan >= std::tan(-67.5 * rad) && Tan < std::tan(-22.5 * rad) && (RenderFlag & 9) == 9)
			{
				TrueFlag = 9;
				ExtraRect.x = (m_UnitRect.m_X + m_UnitRect.m_W + 10000) * Dx;
				ExtraRect.y = (m_UnitRect.m_Y - 60000) * Dy;
				ExtraRect.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
				m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::UPRIGHT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
			}
			if((RenderFlag & 8) == 8 && (Tan < std::tan(-67.5 * rad) || Tan > std::tan(-112.5 * rad)))
			{
				TrueFlag = 8;
				ExtraRect.x = (Heart.x - 25000) * Dx;
				ExtraRect.y = (m_UnitRect.m_Y - 60000) * Dy;
				ExtraRect.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
				m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::UP).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
			}
			if(Tan > 0 && Tan <= std::tan(-157.5 * rad) && (RenderFlag & 2) == 2)
			{
				TrueFlag = 2;
				ExtraRect.x = (m_UnitRect.m_X - 60000) * Dx;
				ExtraRect.y = (Heart.y - 25000) * Dy;
				ExtraRect.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
				m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::LEFT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
	
			}
			if(Tan > std::tan(-157.5 * rad) && Tan <= std::tan(-112.5 * rad) && (RenderFlag & 10) == 10)
			{
				TrueFlag = 10;
				ExtraRect.x = (m_UnitRect.m_X - 60000) * Dx;
				ExtraRect.y = (m_UnitRect.m_Y - 60000) * Dy;
				ExtraRect.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
				m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::UPLEFT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
			}
		}
		else
		{
			if(Tan >= 0 && Tan < std::tan(22.5 * rad) && pTmp->m_AccumulatedDelta.x > 0 && (RenderFlag & 1) == 1)
			{
				TrueFlag = 1;
				ExtraRect.x = (m_UnitRect.m_X + m_UnitRect.m_W + 10000) * Dx;
				ExtraRect.y = (Heart.y - 25000) * Dy;
				ExtraRect.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
				m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::RIGHT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
			}
			if(Tan >= std::tan(22.5 * rad) && Tan < std::tan(67.5 * rad) && (RenderFlag & 5) == 5)
			{
				TrueFlag = 5;
				ExtraRect.x = (m_UnitRect.m_X + m_UnitRect.m_W + 10000) * Dx;
				ExtraRect.y = (m_UnitRect.m_Y + m_UnitRect.m_H + 10000) * Dy;
				ExtraRect.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
				m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::DOWNRIGHT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
			}
			if((RenderFlag & 4) == 4 && (Tan > std::tan(67.5 * rad) || Tan < std::tan(112.5 * rad)))
			{
				TrueFlag = 4;
				ExtraRect.x = (Heart.x - 25000) * Dx;
				ExtraRect.y = (m_UnitRect.m_Y + m_UnitRect.m_H + 10000) * Dy;
				ExtraRect.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
				m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::DOWN).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
			}
			if((RenderFlag & 2) == 2 && Tan <= 0 && Tan > std::tan(157.5 * rad) && m_pBehavior->m_AccumulatedDelta.x < 0)
			{
				TrueFlag = 2;
				ExtraRect.x = (m_UnitRect.m_X - 60000) * Dx;
				ExtraRect.y = (Heart.y - 25000) * Dy;
				ExtraRect.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
				m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::LEFT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
			}
			if((RenderFlag & 6) == 6 && Tan > std::tan(112.5 * rad) && Tan <= std::tan(157.5 * rad))
			{
				TrueFlag = 6;
				ExtraRect.x = (m_UnitRect.m_X - 60000) * Dx;
				ExtraRect.y = (m_UnitRect.m_Y + m_UnitRect.m_H + 10000) * Dy;
				ExtraRect.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
				m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::DOWNLEFT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
			}
		}

		if((RenderFlag & 1) == 1 && TrueFlag != 1)
		{
			ExtraRect.x = (m_UnitRect.m_X + m_UnitRect.m_W + 10000) * Dx;
			ExtraRect.y = (Heart.y - 25000) * Dy;
			ExtraRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
			m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::RIGHT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
		}
		if((RenderFlag & 9) == 9 && TrueFlag != 9)
		{
			ExtraRect.x = (m_UnitRect.m_X + m_UnitRect.m_W + 10000) * Dx;
			ExtraRect.y = (m_UnitRect.m_Y - 60000) * Dy;
			ExtraRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
			m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::UPRIGHT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
		}
		if((RenderFlag & 8) == 8 && TrueFlag != 8)
		{
			ExtraRect.x = (Heart.x - 25000) * Dx;
			ExtraRect.y = (m_UnitRect.m_Y - 60000) * Dy;
			ExtraRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
			m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::UP).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
		}
		if((RenderFlag & 2) == 2 && TrueFlag != 2)
		{
			ExtraRect.x = (m_UnitRect.m_X - 60000) * Dx;
			ExtraRect.y = (Heart.y - 25000) * Dy;
			ExtraRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
			m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::LEFT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
		}
		if((RenderFlag & 10) == 10 && TrueFlag != 10)
		{		
			ExtraRect.x = (m_UnitRect.m_X - 60000) * Dx;
			ExtraRect.y = (m_UnitRect.m_Y - 60000) * Dy;
			ExtraRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
			m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::UPLEFT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
		}
		if((RenderFlag & 5) == 5 && TrueFlag != 5)
		{				
			ExtraRect.x = (m_UnitRect.m_X + m_UnitRect.m_W + 10000) * Dx;				
			ExtraRect.y = (m_UnitRect.m_Y + m_UnitRect.m_H + 10000) * Dy;
			ExtraRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
			m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::DOWNRIGHT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
		}
		if((RenderFlag & 4) == 4 && TrueFlag != 4)
		{
			ExtraRect.x = (Heart.x - 25000) * Dx;
			ExtraRect.y = (m_UnitRect.m_Y + m_UnitRect.m_H + 10000) * Dy;
			ExtraRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
			m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::DOWN).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
		}
		if((RenderFlag & 6) == 6 && TrueFlag != 6)
		{				
			ExtraRect.x = (m_UnitRect.m_X - 60000) * Dx;
			ExtraRect.y = (m_UnitRect.m_Y + m_UnitRect.m_H + 10000) * Dy;
			ExtraRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
			m_pTouchControls->Ui()->DoLabel(&ExtraRect, pTmp->GetLabel(CTouchControls::CBindSlideTouchButtonBehavior::EDirection::DOWNLEFT).m_pLabel, ExtraFontSize, TEXTALIGN_MC, ExtraLabelProps);
		}
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
	for(const auto& Menu : m_vMenus)
	{
		str_format(aBuf, sizeof(aBuf), "%s%s", Menu.second ? "extra-menu-" : "-extra-menu-", Menu.first.c_str());
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
CTouchControls::CExtraMenuTouchButtonBehavior::CExtraMenuTouchButtonBehavior(std::vector<std::string> Menus) :
	CPredefinedTouchButtonBehavior(BEHAVIOR_ID),
	m_vMenus(Menus)
{
	if(m_vMenus[0] == "")
	{
		str_copy(m_aLabel, "\xEF\x83\x89");
	}
	else
	{
		str_format(m_aLabel, sizeof(m_aLabel), "\xEF\x83\x89%s", m_vMenus[0].c_str());
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
		for(const std::string& Menu : m_vMenus)
		m_pTouchControls->m_vMenuMap[Menu] = !m_pTouchControls->m_vMenuMap[Menu];
	}
}

void CTouchControls::CExtraMenuTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	CPredefinedTouchButtonBehavior::WriteToConfiguration(pWriter);

	if(m_vMenus.size() == 1)
	{
		pWriter->WriteAttribute("number");
		pWriter->WriteStrValue(m_vMenus[0].c_str());
	}
	else
	{
		pWriter->WriteAttribute("number");
		pWriter->BeginArray();
		for(const auto& Menu : m_vMenus)
		{
			pWriter->WriteStrValue(Menu.c_str());
		}
		pWriter->EndArray();
	}
}

// Close All Extra Menus button : You know what I mean.
CTouchControls::CButtonLabel CTouchControls::CCloseAllExtraMenuTouchButtonBehavior::GetLabel() const
{
	return {CButtonLabel::EType::PLAIN, "Reset"};
}
void CTouchControls::CCloseAllExtraMenuTouchButtonBehavior::OnDeactivate()
{
	for(auto& Menu : m_pTouchControls->m_vMenuMap)
	Menu.second = false;
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

//Bind slide
CTouchControls::CButtonLabel CTouchControls::CBindSlideTouchButtonBehavior::GetLabel() const
{
	for(const auto &Command: m_vDirCommands)
		if(Command.m_Direction == EDirection::CENTER)
			return {Command.m_LabelType, Command.m_Label.c_str()};
	return {};
}
CTouchControls::CButtonLabel CTouchControls::CBindSlideTouchButtonBehavior::GetLabel(const char *Direction) const
{
	for(const auto &Command: m_vDirCommands)
		if(m_pTouchControls->DIRECTION_NAMES[(int)Command.m_Direction] == Direction)
			return {Command.m_LabelType, Command.m_Label.c_str()};
	return {};
}
CTouchControls::CButtonLabel CTouchControls::CBindSlideTouchButtonBehavior::GetLabel(EDirection Direction) const
{
	for(const auto &Command: m_vDirCommands)
		if(Command.m_Direction == Direction)
			return {Command.m_LabelType, Command.m_Label.c_str()};
	return {};
}
void CTouchControls::CBindSlideTouchButtonBehavior::OnUpdate()
{
	const auto Now = time_get_nanoseconds();		
	if(Now - m_ActivationStartTime >= static_cast<std::chrono::milliseconds>(g_Config.m_ClBindSlideExtraRenderTime))
		m_IsOpen = true;
	if(m_AccumulatedDelta.x*m_AccumulatedDelta.x + m_AccumulatedDelta.y*m_AccumulatedDelta.y >= g_Config.m_ClBindSlideDistance/1000.0f*g_Config.m_ClBindSlideDistance/1000.0f)
		m_IsSliding = true;
	else
		m_IsSliding = false;
}
void CTouchControls::CBindSlideTouchButtonBehavior::OnDeactivate()
{
	CDirCommand Commands[(int)EDirection::NUM_DIRECTIONS];
	for(const auto &Command: m_vDirCommands)
	{
		Commands[(int)Command.m_Direction] = Command;
		Commands[(int)Command.m_Direction].m_IsInit = true;
	}
		
	if(!m_IsSliding)
		m_pTouchControls->Console()->ExecuteLine(Commands[(int)EDirection::CENTER].m_Command.c_str());
	else
	{
		float Tan =(m_AccumulatedDelta.x != 0) ? m_AccumulatedDelta.y / m_AccumulatedDelta.x : 100000.0f;
		float rad = pi / 180.0f;
		//Sliding to different angles, executing different commands.
		if(m_AccumulatedDelta.y < 0.0f)
		{
			if(Tan >= std::tan(-22.5 * rad) && Tan < 0 && Commands[(int)EDirection::RIGHT].m_IsInit)
				m_pTouchControls->Console()->ExecuteLine(Commands[(int)EDirection::RIGHT].m_Command.c_str());
			if(Tan >= std::tan(-67.5 * rad) && Tan < std::tan(-22.5 * rad) && Commands[(int)EDirection::UPRIGHT].m_IsInit)
				m_pTouchControls->Console()->ExecuteLine(Commands[(int)EDirection::UPRIGHT].m_Command.c_str());
			if((Tan < std::tan(-67.5 * rad) || Tan > std::tan(-112.5 * rad)) && Commands[(int)EDirection::UP].m_IsInit)
				m_pTouchControls->Console()->ExecuteLine(Commands[(int)EDirection::UP].m_Command.c_str());
			if(Tan > 0 && Tan <= std::tan(-157.5 * rad) && Commands[(int)EDirection::LEFT].m_IsInit)
				m_pTouchControls->Console()->ExecuteLine(Commands[(int)EDirection::LEFT].m_Command.c_str());
			if(Tan > std::tan(-157.5 * rad) && Tan <= std::tan(-112.5 * rad) && Commands[(int)EDirection::UPLEFT].m_IsInit)
				m_pTouchControls->Console()->ExecuteLine(Commands[(int)EDirection::UPLEFT].m_Command.c_str());
		}
		else
		{
			if(Tan >= 0 && Tan < std::tan(22.5 * rad) && m_AccumulatedDelta.x > 0 && Commands[(int)EDirection::RIGHT].m_IsInit)
				m_pTouchControls->Console()->ExecuteLine(Commands[(int)EDirection::RIGHT].m_Command.c_str());
			if(Tan >= std::tan(22.5 * rad) && Tan < std::tan(67.5 * rad) && Commands[(int)EDirection::DOWNRIGHT].m_IsInit)
				m_pTouchControls->Console()->ExecuteLine(Commands[(int)EDirection::DOWNRIGHT].m_Command.c_str());
			if((Tan > std::tan(67.5 * rad) || Tan < std::tan(112.5 * rad)) && Commands[(int)EDirection::DOWN].m_IsInit)
				m_pTouchControls->Console()->ExecuteLine(Commands[(int)EDirection::DOWN].m_Command.c_str());
			if(Tan <= 0 && Tan > std::tan(157.5 * rad) && m_AccumulatedDelta.x < 0 && Commands[(int)EDirection::LEFT].m_IsInit)
				m_pTouchControls->Console()->ExecuteLine(Commands[(int)EDirection::LEFT].m_Command.c_str());
			if(Tan > std::tan(112.5 * rad) && Tan <= std::tan(157.5 * rad) && Commands[(int)EDirection::DOWNLEFT].m_IsInit)
				m_pTouchControls->Console()->ExecuteLine(Commands[(int)EDirection::DOWNLEFT].m_Command.c_str());
		}
	}
	m_AccumulatedDelta = vec2(0.0f, 0.0f);
	m_IsOpen = false;
	m_IsSliding = false;
}
void CTouchControls::CBindSlideTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	pWriter->WriteAttribute("type");
	pWriter->WriteStrValue(BEHAVIOR_TYPE);

	pWriter->WriteAttribute("commands");
	pWriter->BeginArray();

	for(const auto &Command : m_vDirCommands)
	{
		pWriter->BeginObject();

		pWriter->WriteAttribute("label");
		pWriter->WriteStrValue(Command.m_Label.c_str());

		pWriter->WriteAttribute("label-type");
		pWriter->WriteStrValue(LABEL_TYPE_NAMES[(int)Command.m_LabelType]);

		pWriter->WriteAttribute("direction");
		pWriter->WriteStrValue(DIRECTION_NAMES[(int)Command.m_Direction]);

		pWriter->WriteAttribute("command");
		pWriter->WriteStrValue(Command.m_Command.c_str());

		pWriter->EndObject();
	}

	pWriter->EndArray();
}
//Bar 
CTouchControls::CButtonLabel CTouchControls::CBarTouchButtonBehavior::GetLabel() const
{
	return {CTouchControls::CButtonLabel::EType::PLAIN, m_Label.c_str()};
}
void CTouchControls::CBarTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	pWriter->WriteAttribute("type");
	pWriter->WriteStrValue(BEHAVIOR_TYPE);

	pWriter->WriteAttribute("label");
	pWriter->WriteStrValue(m_Label.c_str());

	pWriter->WriteAttribute("min");
	pWriter->WriteIntValue(m_Min);

	pWriter->WriteAttribute("max");
	pWriter->WriteIntValue(m_Max);

	pWriter->WriteAttribute("target");
	pWriter->WriteStrValue(m_StrTarget.c_str());
}

//Stack Act
void CTouchControls::CStackActTouchButtonBehavior::OnActivate()
{
	if(!m_pTouchControls->m_vCommandStack[m_Number].empty())
	{
		if(m_Current >= (int)m_pTouchControls->m_vCommandStack[m_Number].size())
			m_Current = 0;
		m_pTouchControls->Console()->ExecuteLineStroked(1, m_pTouchControls->m_vCommandStack[m_Number][m_Current].m_Command.c_str());
	}
	else m_pTouchControls->Console()->ExecuteLine("echo Stack is empty!");
		
}
void CTouchControls::CStackActTouchButtonBehavior::OnDeactivate()
{
	m_pTouchControls->Console()->ExecuteLineStroked(0, m_pTouchControls->m_vCommandStack[m_Number][m_Current].m_Command.c_str());
	m_Current = (m_Current + 1) % m_pTouchControls->m_vCommandStack[m_Number].size();
		m_Current = 0;
}
CTouchControls::CButtonLabel CTouchControls::CStackActTouchButtonBehavior::GetLabel()
{
	if(!m_pTouchControls->m_vCommandStack[m_Number].empty())
	{
		if(m_Current >= (int)m_pTouchControls->m_vCommandStack[m_Number].size())
			m_Current = 0;
		return {m_pTouchControls->m_vCommandStack[m_Number][m_Current].m_LabelType, m_pTouchControls->m_vCommandStack[m_Number][m_Current].m_Command.c_str()};
	}
	return {CButtonLabel::EType::PLAIN, ""};
}
void CTouchControls::CStackActTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	pWriter->WriteAttribute("type");
	pWriter->WriteStrValue(BEHAVIOR_TYPE);
	
	pWriter->WriteAttribute("number");
	pWriter->WriteStrValue(m_Number.c_str());
}

//Stack Add
CTouchControls::CButtonLabel CTouchControls::CStackAddTouchButtonBehavior::GetLabel() const
{
	return {m_LabelType, m_Label};
}
void CTouchControls::CStackAddTouchButtonBehavior::OnActivate()
{
	m_pTouchControls->m_vCommandStack[m_Number].insert(m_pTouchControls->m_vCommandStack[m_Number].end(), m_vCommands.begin(), m_vCommands.end());
}
void CTouchControls::CStackAddTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	pWriter->WriteAttribute("type");
	pWriter->WriteStrValue(BEHAVIOR_TYPE);
	
	pWriter->WriteAttribute("label");
	pWriter->WriteStrValue(m_Label);
	
	pWriter->WriteAttribute("label-type");
	pWriter->WriteStrValue(LABEL_TYPE_NAMES[(int)m_LabelType]);
	
	pWriter->WriteAttribute("number");
	pWriter->WriteStrValue(m_Number.c_str());
	
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
//Stack Remove
void CTouchControls::CStackRemoveTouchButtonBehavior::OnActivate()
{
	//-1:remove_all. If there is an order <= 0 on parsing, the m_vOrders will only have one value -1
	if(m_vOrders[0] == -1)
	{
		m_pTouchControls->m_vCommandStack[m_Number].clear();
		return;
	}
	for(const int& Order : m_vOrders)
	{
		if(Order >= (int)m_pTouchControls->m_vCommandStack[m_Number].size())
			continue;
		auto DeleteIndex = m_pTouchControls->m_vCommandStack[m_Number].begin() + Order - 1;
		m_pTouchControls->m_vCommandStack[m_Number].erase(DeleteIndex);
	}
}

CTouchControls::CButtonLabel CTouchControls::CStackRemoveTouchButtonBehavior::GetLabel() const
{
	return {CButtonLabel::EType::ICON, m_Label.c_str()};
}

void CTouchControls::CStackRemoveTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	pWriter->WriteAttribute("type");
	pWriter->WriteStrValue(BEHAVIOR_TYPE);
	
	pWriter->WriteAttribute("number");
	pWriter->WriteStrValue(m_Number.c_str());
	
	pWriter->WriteAttribute("order");
	pWriter->BeginArray();

	for(const int &Order : m_vOrders)
	{
		pWriter->WriteIntValue(Order);
	}
	
	pWriter->EndArray();
}

//Stack Show
//OnActivate for echoing the full label.
void CTouchControls::CStackShowTouchButtonBehavior::OnActivate()
{
	std::string Main;
	if(m_Order >= (int)m_pTouchControls->m_vCommandStack[m_Number].size())
	{
		m_pTouchControls->Console()->ExecuteLine("echo Empty");
		return;
	}
	Main = "echo ";
	Main += m_Prefix.value_or("");
	Main += m_pTouchControls->m_vCommandStack[m_Number][m_Order].m_Label;
	Main += m_Suffix.value_or("");
	m_pTouchControls->Console()->ExecuteLine(Main.c_str());
}
CTouchControls::CButtonLabel CTouchControls::CStackShowTouchButtonBehavior::GetLabel()
{
	std::string Main = "";
	std::string Cut;
	Main += m_Prefix.value_or("");
	if(m_pTouchControls->m_vCommandStack[m_Number][m_Order].m_Label.length() > 15)
		Main += m_pTouchControls->m_vCommandStack[m_Number][m_Order].m_Label.substr(0, 15);
	else
		Main += m_pTouchControls->m_vCommandStack[m_Number][m_Order].m_Label;
	Main += m_Suffix.value_or("");
	m_Tmp = Main;
	return {CButtonLabel::EType::ICON, m_Tmp.c_str()};
}
void CTouchControls::CStackShowTouchButtonBehavior::WriteToConfiguration(CJsonWriter *pWriter)
{
	pWriter->WriteAttribute("type");
	pWriter->WriteStrValue(BEHAVIOR_TYPE);
	
	pWriter->WriteAttribute("number");
	pWriter->WriteStrValue(m_Number.c_str());
	
	pWriter->WriteAttribute("order");
	pWriter->WriteIntValue(m_Order);
	
	if(m_Prefix)
	{
		pWriter->WriteAttribute("prefix");
		pWriter->WriteStrValue(m_Prefix.value().c_str());
	}
	
	if(m_Suffix)
	{
		pWriter->WriteAttribute("suffix");
		pWriter->WriteStrValue(m_Suffix.value().c_str());
	}
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
			const auto TriggerButton = std::find_if(m_vTouchButtons.begin(), m_vTouchButtons.end(), [&](const CTouchButton &Button) {
					CBindSlideTouchButtonBehavior* pTmp = nullptr;
					pTmp = dynamic_cast<CBindSlideTouchButtonBehavior*>(Button.m_pBehavior.get());
					return Button.m_pBehavior->IsActive(TouchFingerState.m_Finger) &&
					       pTmp;
				});
			return TouchButton.m_VisibilityStartTime < TouchFingerState.m_PressTime &&
			       TouchButton.IsInside(TouchFingerState.m_Position * ScreenSize) &&
			       TriggerButton == m_vTouchButtons.end();
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
	}
	for(CTouchButton &TouchButton : m_vTouchButtons)
	{
		if(!TouchButton.IsVisible())
			continue;
		TouchButton.UpdateBackgroundCorners();
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

	std::optional<ColorRGBA> ParsedBackgroundColorInactive =
		ParseColor(&(*pConfiguration)["background-color-inactive"], "background-color-inactive", ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f));
	if(!ParsedBackgroundColorInactive.has_value())
	{
		json_value_free(pConfiguration);
		return false;
	}

	std::optional<ColorRGBA> ParsedBackgroundColorActive =
		ParseColor(&(*pConfiguration)["background-color-active"], "background-color-active", ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f));
	if(!ParsedBackgroundColorActive.has_value())
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
	m_BackgroundColorInactive = ParsedBackgroundColorInactive.value();
	m_BackgroundColorActive = ParsedBackgroundColorActive.value();

	m_pPrimaryJoystickTouchButtonBehavior = nullptr;
	m_vTouchButtons = std::move(vParsedTouchButtons);
	for(CTouchButton &TouchButton : m_vTouchButtons)
	{
		TouchButton.UpdatePointers();
		TouchButton.UpdateScreenFromUnitRect();
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

std::optional<ColorRGBA> CTouchControls::ParseColor(const json_value *pColorValue, const char *pAttributeName, std::optional<ColorRGBA> DefaultColor) const
{
	const json_value &Color = *pColorValue;
	if(Color.type == json_none && DefaultColor.has_value())
	{
		return DefaultColor;
	}
	if(Color.type != json_string)
	{
		log_error("touch_controls", "Failed to parse configuration: attribute '%s' must specify a string", pAttributeName);
		return {};
	}
	std::optional<ColorRGBA> ParsedColor = color_parse<ColorRGBA>(Color.u.string.ptr);
	if(!ParsedColor.has_value())
	{
		log_error("touch_controls", "Failed to parse configuration: attribute '%s' specifies invalid color value '%s'", pAttributeName, Color.u.string.ptr);
		return {};
	}
	return ParsedColor;
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
	std::unordered_map<std::string, bool> vParsedMenus;
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
		std::string VisibilityString(pVisibilityString);
		
		if(str_comp(pVisibilityString, "extra-menu") == 0)
		{
			m_vMenuMap["1"] = false;
			vParsedMenus["1"] = ParsedParity;
			continue;
		}
		if(VisibilityString.compare(0, 11, "extra-menu-") == 0)
		{
			std::string MenuString = VisibilityString.substr(11);
			if(std::any_of(vParsedMenus.begin(), vParsedMenus.end(), [&](auto OtherMenu) {
				return str_comp(OtherMenu.first.c_str(), MenuString.c_str()) == 0;
			}))
			{
				log_error("touch_controls", "Failed to parse touch button: attribute 'visibilities' specifies duplicate value '%s' at '%d'", pVisibilityString, VisibilityIndex);
				return {};
			}
			if(m_vMenuMap.find(MenuString) != m_vMenuMap.end())
			m_vMenuMap[MenuString] = false;
			vParsedMenus[MenuString] = ParsedParity;
			
			continue;
		}
		else
		{
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
	Button.m_vMenus = std::move(vParsedMenus);
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
	else if(str_comp(BehaviorType.u.string.ptr, CBindSlideTouchButtonBehavior::BEHAVIOR_TYPE) == 0)
	{
		return ParseBindSlideBehavior(&BehaviorObject);
	}
	else if(str_comp(BehaviorType.u.string.ptr, CBarTouchButtonBehavior::BEHAVIOR_TYPE) == 0)
	{
		return ParseBarBehavior(&BehaviorObject);
	}
	else if(str_comp(BehaviorType.u.string.ptr, CStackActTouchButtonBehavior::BEHAVIOR_TYPE) == 0)
	{
		return ParseStackActBehavior(&BehaviorObject);
	}
	else if(str_comp(BehaviorType.u.string.ptr, CStackAddTouchButtonBehavior::BEHAVIOR_TYPE) == 0)
	{
		return ParseStackAddBehavior(&BehaviorObject);
	}
	else if(str_comp(BehaviorType.u.string.ptr, CStackRemoveTouchButtonBehavior::BEHAVIOR_TYPE) == 0)
	{
		return ParseStackRemoveBehavior(&BehaviorObject);
	}
	else if(str_comp(BehaviorType.u.string.ptr, CStackShowTouchButtonBehavior::BEHAVIOR_TYPE) == 0)
	{
		return ParseStackShowBehavior(&BehaviorObject);
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
	std::vector<std::string> ParsedMenuNumber;
	bool Flag = false;
	const json_value &BehaviorObject = *pBehaviorObject;
	const json_value &MenuNumber = BehaviorObject["number"];
	std::string TmpString;
	m_vMenuMap["1"] = false;
	if(MenuNumber.type == json_none)
	{
		ParsedMenuNumber.emplace_back("1");
	}
	else if(MenuNumber.type == json_integer)
	{
		TmpString = std::to_string(MenuNumber.u.integer);
		ParsedMenuNumber.emplace_back(TmpString);
		if(m_vMenuMap.find(TmpString) != m_vMenuMap.end())
			m_vMenuMap[TmpString] = false;
	}
	else if(MenuNumber.type == json_string)
	{
		TmpString = MenuNumber.u.string.ptr;
		ParsedMenuNumber.emplace_back(TmpString);
		if(m_vMenuMap.find(TmpString) != m_vMenuMap.end())
			m_vMenuMap[TmpString] = false;
	}
	else if(MenuNumber.type == json_array)
		for(unsigned MenuIndex = 0; MenuIndex < MenuNumber.u.array.length; ++MenuIndex)
		{
			const json_value &Menu = MenuNumber[MenuIndex];
			if(Menu.type == json_none)
				ParsedMenuNumber.emplace_back("1");
			else if(Menu.type == json_integer)
			{
				TmpString = std::to_string(MenuNumber.u.integer);
				ParsedMenuNumber.emplace_back(TmpString);
				if(m_vMenuMap.find(TmpString) != m_vMenuMap.end())
					m_vMenuMap[TmpString] = false;
			}
			else if(Menu.type == json_string)
			{
				TmpString = Menu.u.string.ptr;
				ParsedMenuNumber.emplace_back(TmpString);
				if(m_vMenuMap.find(TmpString) != m_vMenuMap.end())
					m_vMenuMap[TmpString] = false;
			}
			else
			{
				Flag = true;
				break;
			}
		}
	else
		Flag = true;
	if(Flag == true)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s' and ID '%s': attribute 'number' specify unknown type of values",
			CPredefinedTouchButtonBehavior::BEHAVIOR_TYPE, CExtraMenuTouchButtonBehavior::BEHAVIOR_ID);
		return nullptr;
	}
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
	if(LabelType.type != json_string && LabelType.type != json_none)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'label-type' must specify a string", CBindTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	CButtonLabel::EType ParsedLabelType = CButtonLabel::EType::NUM_TYPES;
	if(LabelType.type == json_none)
	{
		ParsedLabelType = CButtonLabel::EType::PLAIN;
	}
	else for(int CurrentType = (int)CButtonLabel::EType::PLAIN; CurrentType < (int)CButtonLabel::EType::NUM_TYPES; ++CurrentType)
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

std::unique_ptr<CTouchControls::CBindSlideTouchButtonBehavior> CTouchControls::ParseBindSlideBehavior(const json_value *pBehaviorObject)
{
	const json_value &CommandsObject = (*pBehaviorObject)["commands"];
	bool flag = false;
	if(CommandsObject.type != json_array)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'commands' must specify an array", CBindSlideTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}

	std::vector<CTouchControls::CBindSlideTouchButtonBehavior::CDirCommand> vCommands;
	vCommands.reserve(CommandsObject.u.array.length);
	for(unsigned CommandIndex = 0; CommandIndex < CommandsObject.u.array.length; ++CommandIndex)
	{
		const json_value &CommandObject = CommandsObject[CommandIndex];
		if(CommandObject.type != json_object)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'commands' must specify an array of objects", CBindSlideTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return nullptr;
		}

		const json_value &Label = CommandObject["label"];
		if(Label.type != json_string)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'label' must specify a string", CBindSlideTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return nullptr;
		}

		const json_value &LabelType = CommandObject["label-type"];
		if(LabelType.type != json_string)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'label-type' must specify a string", CBindSlideTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return {};
		}
		const json_value &Direction = CommandObject["direction"];
		if(Direction.type != json_string)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'direction' must specify a string", CBindSlideTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return nullptr;
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
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'label-type' specifies unknown value '%s'", CBindSlideTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex, LabelType.u.string.ptr);
			return {};
		}
		CBindSlideTouchButtonBehavior::EDirection ParsedDirection = CBindSlideTouchButtonBehavior::EDirection::NUM_DIRECTIONS;
		for(int CurrentDir = (int)CBindSlideTouchButtonBehavior::EDirection::LEFT; CurrentDir < (int)CBindSlideTouchButtonBehavior::EDirection::NUM_DIRECTIONS; ++CurrentDir)
		{
			if(str_comp(Direction.u.string.ptr, DIRECTION_NAMES[CurrentDir]) == 0)
			{
				ParsedDirection = (CBindSlideTouchButtonBehavior::EDirection)CurrentDir;
				if(str_comp(Direction.u.string.ptr, DIRECTION_NAMES[8]) == 0)
					flag = true;
				break;
			}
		}
		if(ParsedDirection == CBindSlideTouchButtonBehavior::EDirection::NUM_DIRECTIONS)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'direction' specifies unknown value '%s'", CBindSlideTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex, Direction.u.string.ptr);
			return {};
		}

		const json_value &Command = CommandObject["command"];
		if(Command.type != json_string)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'command' must specify a string", CBindSlideTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return nullptr;
		}
		vCommands.emplace_back(Label.u.string.ptr, ParsedLabelType, ParsedDirection, Command.u.string.ptr);
	}
	if(!flag)
	{
		log_error("touch_controls", "Center is missing");
		return nullptr;
	}
	return std::make_unique<CBindSlideTouchButtonBehavior>(std::move(vCommands));
}

std::unique_ptr<CTouchControls::CBarTouchButtonBehavior> CTouchControls::ParseBarBehavior(const json_value *pBehaviorObject)
{
	const json_value &BehaviorObject = *pBehaviorObject;
	const json_value &Label = BehaviorObject["label"];
	if(Label.type != json_string)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'label' must specify a string", CBarTouchButtonBehavior::BEHAVIOR_TYPE);
		return nullptr;
	}

	const json_value &Min = BehaviorObject["min"];
	if(Min.type != json_integer)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'min' must specify an integer", CBarTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	const json_value &Max = BehaviorObject["max"];
	if(Max.type != json_integer)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'max' must specify an integer", CBarTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}

	const json_value &Target = BehaviorObject["target"];
	if(Target.type != json_string)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'target' must specify a string", CBarTouchButtonBehavior::BEHAVIOR_TYPE);
		return nullptr;
	}

	SuperMap::Init();
	auto Find = SuperMap::Map.find(Target.u.string.ptr);
	if(Find == SuperMap::Map.end())
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'target' specify an unknown string", CBarTouchButtonBehavior::BEHAVIOR_TYPE);
		return nullptr;
	}
	return std::make_unique<CBarTouchButtonBehavior>(Label.u.string.ptr, Min.u.integer, Max.u.integer, Find->second, Find->first);
}

std::unique_ptr<CTouchControls::CStackActTouchButtonBehavior> CTouchControls::ParseStackActBehavior(const json_value *pBehaviorObject)
{
	const json_value &BehaviorObject = *pBehaviorObject;
	const json_value &Number = BehaviorObject["number"];
	std::string TmpString;
	if(Number.type == json_integer)
	{
		TmpString = std::to_string(Number.u.integer);
	}
	else if(Number.type == json_none)
	{
		TmpString = "";
	}
	else if(Number.type == json_string)
	{
		TmpString = Number.u.string.ptr;
	}
	else
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'number' specify an unknown type value", CStackActTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	if(m_vCommandStack.find(TmpString) != m_vCommandStack.end())
	{
		m_vCommandStack[TmpString];
	}
	return std::make_unique<CStackActTouchButtonBehavior>(TmpString);
}

std::unique_ptr<CTouchControls::CStackAddTouchButtonBehavior> CTouchControls::ParseStackAddBehavior(const json_value *pBehaviorObject)
{
	const json_value &BehaviorObject = *pBehaviorObject;
	const json_value &Number = BehaviorObject["number"];
	std::string ParsedNumber;
	if(Number.type == json_integer)
	{
		ParsedNumber = std::to_string(Number.u.integer);
	}
	else if(Number.type == json_none)
	{
		ParsedNumber = "";
	}
	else if(Number.type == json_string)
	{
		ParsedNumber = Number.u.string.ptr;
	}
	else
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'number' specify an unknown type value", CStackAddTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	const json_value &Label = BehaviorObject["label"];
	if(Label.type != json_string)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'label' must specify a string", CStackAddTouchButtonBehavior::BEHAVIOR_TYPE);
		return nullptr;
	}
	
	const json_value &LabelType = BehaviorObject["label-type"];
	if(LabelType.type != json_string && LabelType.type != json_none)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'label-type' must specify a string", CStackAddTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	CButtonLabel::EType ParsedLabelType = CButtonLabel::EType::NUM_TYPES;
	if(LabelType.type == json_none)
	{
		ParsedLabelType = CButtonLabel::EType::PLAIN;
	}
	else for(int CurrentType = (int)CButtonLabel::EType::PLAIN; CurrentType < (int)CButtonLabel::EType::NUM_TYPES; ++CurrentType)
	{
		if(str_comp(LabelType.u.string.ptr, LABEL_TYPE_NAMES[CurrentType]) == 0)
		{
			ParsedLabelType = (CButtonLabel::EType)CurrentType;
			break;
		}
	}
	if(ParsedLabelType == CButtonLabel::EType::NUM_TYPES)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'label-type' specifies unknown value '%s'", CStackAddTouchButtonBehavior::BEHAVIOR_TYPE, LabelType.u.string.ptr);
		return {};
	}
	std::vector<CTouchControls::CStackAddTouchButtonBehavior::CCommand> vCommands;
	const json_value &CommandsObject = BehaviorObject["commands"];
	if(CommandsObject.type != json_array)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'commands' must specify an array", CStackAddTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	vCommands.reserve(CommandsObject.u.array.length);
	for(unsigned CommandIndex = 0; CommandIndex < CommandsObject.u.array.length; ++CommandIndex)
	{
		const json_value &CommandObject = CommandsObject[CommandIndex];
		if(CommandObject.type != json_object)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'commands' must specify an array of objects", CStackAddTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return nullptr;
		}

		const json_value &CommandLabel = CommandObject["label"];
		if(CommandLabel.type != json_string)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'label' must specify a string", CStackAddTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return nullptr;
		}
		
		const json_value &CommandLabelType = CommandObject["label-type"];
		CButtonLabel::EType ParsedCommandLabelType = CButtonLabel::EType::NUM_TYPES;
		if(CommandLabelType.type == json_none)
		{
			ParsedCommandLabelType = CButtonLabel::EType::PLAIN;
		}
		else if(CommandLabelType.type == json_string)
		{
			for(int CurrentType = (int)CButtonLabel::EType::PLAIN; CurrentType < (int)CButtonLabel::EType::NUM_TYPES; ++CurrentType)
				if(str_comp(CommandLabelType.u.string.ptr, LABEL_TYPE_NAMES[CurrentType]) == 0)
				{
					ParsedLabelType = (CButtonLabel::EType)CurrentType;
					break;
				}
			if(ParsedLabelType == CButtonLabel::EType::NUM_TYPES)
			{
				log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'label-type' specifies unknown value '%s'", CStackAddTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex, LabelType.u.string.ptr);
				return {};
			}
		}
		else
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'label-type' must specify a string", CStackAddTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return {};
		}
		const json_value &Command = CommandObject["command"];
		if(Command.type != json_string)
		{
			log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d': attribute 'command' must specify a string", CStackAddTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
			return {};
		}
		vCommands.emplace_back(CommandLabel.u.string.ptr, ParsedCommandLabelType, Command.u.string.ptr);
	}
	return std::make_unique<CStackAddTouchButtonBehavior>(ParsedNumber, Label.u.string.ptr, ParsedLabelType, std::move(vCommands));
}

std::unique_ptr<CTouchControls::CStackRemoveTouchButtonBehavior> CTouchControls::ParseStackRemoveBehavior(const json_value *pBehaviorObject)
{
	const json_value &BehaviorObject = *pBehaviorObject;
	const json_value &Number = BehaviorObject["number"];
	std::string ParsedNumber;
	if(Number.type == json_integer)
	{
		ParsedNumber = std::to_string(Number.u.integer);
	}
	else if(Number.type == json_none)
	{
		ParsedNumber = "";
	}
	else if(Number.type == json_string)
	{
		ParsedNumber = Number.u.string.ptr;
	}
	else
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'number' specify an unknown type value", CStackRemoveTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	const json_value &Orders = BehaviorObject["order"];
	std::vector<int> vParsedOrders;
	if(Orders.type == json_integer)
	{
		int Check = (Orders.u.integer <= 0) ? -1 : Orders.u.integer;
		vParsedOrders.emplace_back(Check);
	}
	else if(Orders.type == json_array)
	{
		vParsedOrders.reserve(Orders.u.array.length);
		for(unsigned CommandIndex = 0; CommandIndex < Orders.u.array.length; ++CommandIndex)
		{
			const json_value &Order = Orders[CommandIndex];
			if(Order.type != json_integer)
			{
				log_error("touch_controls", "Failed to parse touch button behavior of type '%s': failed to parse command at index '%d' attribute 'order' specify an unknown type value. Only integers are allowed in this array", CStackRemoveTouchButtonBehavior::BEHAVIOR_TYPE, CommandIndex);
				return {};
			}
			else if(Order.u.integer <= 0)
			{
				vParsedOrders.clear();
				vParsedOrders.reserve(1);
				vParsedOrders.emplace_back(-1);
				break;
			}
			else
			{
				bool CheckIfRepeat = std::any_of(vParsedOrders.begin(), vParsedOrders.end(), [Order](const int& Element){
					return Order.u.integer == Element;
				});
				if(CheckIfRepeat)
					continue;
				vParsedOrders.emplace_back(Order.u.integer);
			}
		}
	}
	else
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'order' specify an unknown type value", CStackRemoveTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	std::string ParsedLabel;
	char aBuf[256];
	std::string bBuf = "";
	if(vParsedOrders[0] == -1)
	{
		ParsedLabel = "\xEF\x81\x97" "All";
	}
	else
	{
		std::sort(vParsedOrders.begin(), vParsedOrders.end(), [](int ElementA, int ElementB){
			return ElementA > ElementB;
		});
		if(vParsedOrders.size() > 5)
			ParsedLabel = "\xEF\x81\x97...";
		else
		{
			for(int Index = vParsedOrders.size() - 1; Index >= 0; Index --)
			{
				str_format(aBuf, sizeof(aBuf), "%d %s", vParsedOrders[Index], bBuf.c_str());
				bBuf = aBuf;
			}
			str_format(aBuf, sizeof(aBuf), "\xEF\x81\x97 %s", bBuf.c_str());
			ParsedLabel = aBuf;
		}
	}
	
	return std::make_unique<CStackRemoveTouchButtonBehavior>(ParsedNumber, std::move(vParsedOrders), ParsedLabel);
}

std::unique_ptr<CTouchControls::CStackShowTouchButtonBehavior> CTouchControls::ParseStackShowBehavior(const json_value *pBehaviorObject)
{
	const json_value &BehaviorObject = *pBehaviorObject;
	const json_value &Number = BehaviorObject["number"];
	std::string ParsedNumber;
	if(Number.type == json_integer)
	{
		ParsedNumber = std::to_string(Number.u.integer);
	}
	else if(Number.type == json_none)
	{
		ParsedNumber = "";
	}
	else if(Number.type == json_string)
	{
		ParsedNumber = Number.u.string.ptr;
	}
	else
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'number' specify an unknown type value", CStackShowTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	
	const json_value &Order = BehaviorObject["order"];
	if(Order.type != json_integer)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'order' must specify an integer", CStackShowTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	
	const json_value &Prefix = BehaviorObject["prefix"];
	std::optional<std::string> ParsedPrefix;
	if(Prefix.type == json_string)
	{
		ParsedPrefix.emplace(Prefix.u.string.ptr);
	}
	else if(Prefix.type != json_none)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'prefix' must specify a string", CStackShowTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	
	const json_value &Suffix = BehaviorObject["suffix"];
	std::optional<std::string> ParsedSuffix;
	if(Suffix.type == json_string)
	{
		ParsedSuffix.emplace(Suffix.u.string.ptr);
	}
	else if(Suffix.type != json_none)
	{
		log_error("touch_controls", "Failed to parse touch button behavior of type '%s': attribute 'suffix' must specify a string", CStackShowTouchButtonBehavior::BEHAVIOR_TYPE);
		return {};
	}
	
	return std::make_unique<CStackShowTouchButtonBehavior>(ParsedNumber, Order, ParsedPrefix, ParsedSuffix);
}

void CTouchControls::WriteConfiguration(CJsonWriter *pWriter)
{
	pWriter->BeginObject();

	pWriter->WriteAttribute("direct-touch-ingame");
	pWriter->WriteStrValue(DIRECT_TOUCH_INGAME_MODE_NAMES[(int)m_DirectTouchIngame]);

	pWriter->WriteAttribute("direct-touch-spectate");
	pWriter->WriteStrValue(DIRECT_TOUCH_SPECTATE_MODE_NAMES[(int)m_DirectTouchSpectate]);

	char aColor[9];
	str_format(aColor, sizeof(aColor), "%08X", m_BackgroundColorInactive.PackAlphaLast());
	pWriter->WriteAttribute("background-color-inactive");
	pWriter->WriteStrValue(aColor);

	str_format(aColor, sizeof(aColor), "%08X", m_BackgroundColorActive.PackAlphaLast());
	pWriter->WriteAttribute("background-color-active");
	pWriter->WriteStrValue(aColor);

	pWriter->WriteAttribute("touch-buttons");
	pWriter->BeginArray();
	for(CTouchButton &TouchButton : m_vTouchButtons)
	{
		TouchButton.WriteToConfiguration(pWriter);
	}
	pWriter->EndArray();

	pWriter->EndObject();
}
