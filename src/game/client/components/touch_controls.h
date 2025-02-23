#ifndef GAME_CLIENT_COMPONENTS_TOUCH_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_TOUCH_CONTROLS_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/input.h>

#include <engine/shared/config.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

class CJsonWriter;
typedef struct _json_value json_value;

class CTouchControls : public CComponent
{
public:
	enum class EDirectTouchIngameMode
	{
		DISABLED,
		ACTION,
		AIM,
		FIRE,
		HOOK,
		NUM_STATES
	};
	enum class EDirectTouchSpectateMode
	{
		DISABLED,
		AIM,
		NUM_STATES
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnWindowResize() override;
	bool OnTouchState(const std::vector<IInput::CTouchFingerState> &vTouchFingerStates) override;
	void OnRender() override;

	bool LoadConfigurationFromFile(int StorageType);
	bool LoadConfigurationFromClipboard();
	bool SaveConfigurationToFile();
	void SaveConfigurationToClipboard();

	EDirectTouchIngameMode DirectTouchIngame() const { return m_DirectTouchIngame; }
	void SetDirectTouchIngame(EDirectTouchIngameMode DirectTouchIngame)
	{
		m_DirectTouchIngame = DirectTouchIngame;
		m_EditingChanges = true;
	}
	EDirectTouchSpectateMode DirectTouchSpectate() const { return m_DirectTouchSpectate; }
	void SetDirectTouchSpectate(EDirectTouchSpectateMode DirectTouchSpectate)
	{
		m_DirectTouchSpectate = DirectTouchSpectate;
		m_EditingChanges = true;
	}
	bool IsEditingActive() const { return m_EditingActive; }
	void SetEditingActive(bool EditingActive) { m_EditingActive = EditingActive; }
	bool HasEditingChanges() const { return m_EditingChanges; }
	void SetEditingChanges(bool EditingChanges) { m_EditingChanges = EditingChanges; }

private:
	static constexpr const char *const DIRECT_TOUCH_INGAME_MODE_NAMES[(int)EDirectTouchIngameMode::NUM_STATES] = {"disabled", "action", "aim", "fire", "hook"};
	static constexpr const char *const DIRECT_TOUCH_SPECTATE_MODE_NAMES[(int)EDirectTouchSpectateMode::NUM_STATES] = {"disabled", "aim"};

	enum class EButtonShape
	{
		RECT,
		CIRCLE,
		STAR,
		SSTAR,
		NUM_SHAPES
	};

	static constexpr const char *const SHAPE_NAMES[(int)EButtonShape::NUM_SHAPES] = {"rect", "circle", "star","sstar"};

	enum class EButtonVisibility
	{
		INGAME,
		ZOOM_ALLOWED,
		VOTE_ACTIVE,
		DUMMY_ALLOWED,
		DUMMY_CONNECTED,
		RCON_AUTHED,
		DEMO_PLAYER,
		NUM_VISIBILITIES
	};
	
	std::unordered_map<std::string, bool> m_vMenuMap;
	
	class StackAddTouchButtonBehavior;
	class StackAddTouchButtonBehavior::CCommand;
	std::unordered_map<std::string, std::vector<StackAddTouchButtonBehavior::CCommand>> m_vCommandStack;
	

	class CButtonVisibility
	{
	public:
		EButtonVisibility m_Type;
		bool m_Parity;

		CButtonVisibility(EButtonVisibility Type, bool Parity) :
			m_Type(Type), m_Parity(Parity) {}
	};

	class CButtonVisibilityData
	{
	public:
		const char *m_pId;
		std::function<bool()> m_Function;
	};

	CButtonVisibilityData m_aVisibilityFunctions[(int)EButtonVisibility::NUM_VISIBILITIES];

	enum
	{
		ACTION_AIM,
		ACTION_FIRE,
		ACTION_HOOK,
		NUM_ACTIONS
	};

	class CButtonLabel
	{
	public:
		enum class EType
		{
			/**
			 * Label is used as is.
			 */
			PLAIN,
			/**
			 * Label is localized. Only usable for default button labels for which there must be
			 * corresponding `Localizable`-calls in code and string in the translation files.
			 */
			LOCALIZED,
			/**
			 * Icon font is used for the label.
			 */
			ICON,
			/**
   			* RAINBOW COLOR PLAIN LABELS
      			*/
			RAINBOW,
			/**
			 * Number of label types.
			 */
			NUM_TYPES
		};

		EType m_Type;
		const char *m_pLabel;
	};

	static constexpr const char *const LABEL_TYPE_NAMES[(int)CButtonLabel::EType::NUM_TYPES] = {"plain", "localized", "icon", "rainbow"};

	class CUnitRect
	{
	public:
		int m_X;
		int m_Y;
		int m_W;
		int m_H;
	};

	class CTouchButtonBehavior;

	class CTouchButton
	{
	public:
		CTouchButton(CTouchControls *pTouchControls);
		CTouchButton(CTouchButton &&Other) noexcept;
		CTouchButton(const CTouchButton &Other) = delete;

		CTouchButton &operator=(const CTouchButton &Other) = delete;
		CTouchButton &operator=(CTouchButton &&Other) noexcept;


		CTouchControls *m_pTouchControls;

		CUnitRect m_UnitRect;
		CUIRect m_ScreenRect;

		EButtonShape m_Shape;
		int m_BackgroundCorners; // only used with EButtonShape::RECT

		std::vector<CButtonVisibility> m_vVisibilities;
		std::unordered_map<std::string, bool> m_vMenus;
		std::unique_ptr<CTouchButtonBehavior> m_pBehavior;

		bool m_VisibilityCached;
		std::chrono::nanoseconds m_VisibilityStartTime;

		void UpdatePointers();
		void UpdateScreenFromUnitRect();
		void UpdateBackgroundCorners();

		vec2 ClampTouchPosition(vec2 TouchPosition) const;
		bool IsInside(vec2 TouchPosition) const;
		void UpdateVisibility();
		bool IsVisible() const;
		bool m_ExtraRender = false;
		void Render();
		void WriteToConfiguration(CJsonWriter *pWriter);
	};

	class CTouchButtonBehavior
	{
	public:
		CTouchButton *m_pTouchButton;
		CTouchControls *m_pTouchControls;

		bool m_Active; // variables below must only be used when active
		IInput::CTouchFinger m_Finger;
		vec2 m_ActivePosition;
		vec2 m_AccumulatedDelta;
		std::chrono::nanoseconds m_ActivationStartTime;

		virtual ~CTouchButtonBehavior() = default;
		virtual void Init(CTouchButton *pTouchButton);

		void Reset();
		void SetActive(const IInput::CTouchFingerState &FingerState);
		void SetInactive();
		bool IsActive() const;
		bool IsActive(const IInput::CTouchFinger &Finger) const;

		virtual CButtonLabel GetLabel() const = 0;
		virtual void OnActivate() {}
		virtual void OnDeactivate() {}
		virtual void OnUpdate() {}
		virtual void WriteToConfiguration(CJsonWriter *pWriter) = 0;
		
		virtual std::string GetType() {
			return "main";
		}
	};

	/**
	 * Abstract class for predefined behaviors.
	 *
	 * Subclasses must implemented the concrete behavior and provide the label.
	 */
	class CPredefinedTouchButtonBehavior : public CTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_TYPE = "predefined";

		CPredefinedTouchButtonBehavior(const char *pId) :
			m_pId(pId) {}

		/**
		 * Implements the serialization for predefined behaviors. Subclasses
		 * may override this, but they should call the parent function first.
		 */
		void WriteToConfiguration(CJsonWriter *pWriter) override;
		std::string GetType() override {
			return BEHAVIOR_TYPE;
		}
	private:
		const char *m_pId;
	};

	class CIngameMenuTouchButtonBehavior : public CPredefinedTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_ID = "ingame-menu";

		CIngameMenuTouchButtonBehavior() :
			CPredefinedTouchButtonBehavior(BEHAVIOR_ID) {}

		CButtonLabel GetLabel() const override;
		void OnDeactivate() override;
	};

	class CExtraMenuTouchButtonBehavior : public CPredefinedTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_ID = "extra-menu";

		CExtraMenuTouchButtonBehavior(std::vector<std::string> Menus);

		CButtonLabel GetLabel() const override;
		void OnDeactivate() override;
		void WriteToConfiguration(CJsonWriter *pWriter) override;
		std::vector<std::string> m_vMenus;
		char m_aLabel[64];
	};

	class CCloseAllExtraMenuTouchButtonBehavior : public CPredefinedTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_ID = "close-extra-menu";
		
		CCloseAllExtraMenuTouchButtonBehavior() :
			CPredefinedTouchButtonBehavior(BEHAVIOR_ID) {}
		
		CButtonLabel GetLabel() const override;
		void OnDeactivate() override;
	};

	class CEmoticonTouchButtonBehavior : public CPredefinedTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_ID = "emoticon";

		CEmoticonTouchButtonBehavior() :
			CPredefinedTouchButtonBehavior(BEHAVIOR_ID) {}

		CButtonLabel GetLabel() const override;
		void OnDeactivate() override;
	};

	class CSpectateTouchButtonBehavior : public CPredefinedTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_ID = "spectate";

		CSpectateTouchButtonBehavior() :
			CPredefinedTouchButtonBehavior(BEHAVIOR_ID) {}

		CButtonLabel GetLabel() const override;
		void OnDeactivate() override;
	};

	class CSwapActionTouchButtonBehavior : public CPredefinedTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_ID = "swap-action";

		CSwapActionTouchButtonBehavior() :
			CPredefinedTouchButtonBehavior(BEHAVIOR_ID) {}

		CButtonLabel GetLabel() const override;
		void OnActivate() override;
		void OnDeactivate() override;

	private:
		int m_ActiveAction = NUM_ACTIONS;
	};

	class CUseActionTouchButtonBehavior : public CPredefinedTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_ID = "use-action";

		CUseActionTouchButtonBehavior() :
			CPredefinedTouchButtonBehavior(BEHAVIOR_ID) {}

		CButtonLabel GetLabel() const override;
		void OnActivate() override;
		void OnDeactivate() override;

	private:
		int m_ActiveAction = NUM_ACTIONS;
	};

	class CJoystickTouchButtonBehavior : public CPredefinedTouchButtonBehavior
	{
	public:
		CJoystickTouchButtonBehavior(const char *pId) :
			CPredefinedTouchButtonBehavior(pId) {}

		CButtonLabel GetLabel() const override;
		void OnActivate() override;
		void OnDeactivate() override;
		void OnUpdate() override;
		int ActiveAction() const { return m_ActiveAction; }
		virtual int SelectedAction() const = 0;

	private:
		int m_ActiveAction = NUM_ACTIONS;
	};

	class CJoystickActionTouchButtonBehavior : public CJoystickTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_ID = "joystick-action";

		CJoystickActionTouchButtonBehavior() :
			CJoystickTouchButtonBehavior(BEHAVIOR_ID) {}

		void Init(CTouchButton *pTouchButton) override;
		int SelectedAction() const override;
	};

	class CJoystickAimTouchButtonBehavior : public CJoystickTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_ID = "joystick-aim";

		CJoystickAimTouchButtonBehavior() :
			CJoystickTouchButtonBehavior(BEHAVIOR_ID) {}

		int SelectedAction() const override;
	};

	class CJoystickFireTouchButtonBehavior : public CJoystickTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_ID = "joystick-fire";

		CJoystickFireTouchButtonBehavior() :
			CJoystickTouchButtonBehavior(BEHAVIOR_ID) {}

		int SelectedAction() const override;
	};

	class CJoystickHookTouchButtonBehavior : public CJoystickTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_ID = "joystick-hook";

		CJoystickHookTouchButtonBehavior() :
			CJoystickTouchButtonBehavior(BEHAVIOR_ID) {}

		int SelectedAction() const override;
	};

	/**
	 * Generic behavior implementation that executes a console command like a bind.
	 */
	class CBindTouchButtonBehavior : public CTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_TYPE = "bind";

		CBindTouchButtonBehavior(const char *pLabel, CButtonLabel::EType LabelType, const char *pCommand) :
			m_Label(pLabel),
			m_LabelType(LabelType),
			m_Command(pCommand) {}

		CButtonLabel GetLabel() const override;
		void OnActivate() override;
		void OnDeactivate() override;
		void OnUpdate() override;
		void WriteToConfiguration(CJsonWriter *pWriter) override;
		std::string GetType() override {
			return BEHAVIOR_TYPE;
		}

	private:
		std::string m_Label;
		CButtonLabel::EType m_LabelType;
		std::string m_Command;

		bool m_Repeating = false;
		std::chrono::nanoseconds m_LastUpdateTime;
		std::chrono::nanoseconds m_AccumulatedRepeatingTime;
	};

	/**
	 * Generic behavior implementation that switches between executing one of two or more console commands.
	 */
	class CBindToggleTouchButtonBehavior : public CTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_TYPE = "bind-toggle";

		class CCommand
		{
		public:
			std::string m_Label;
			CButtonLabel::EType m_LabelType;
			std::string m_Command;

			CCommand(const char *pLabel, CButtonLabel::EType LabelType, const char *pCommand) :
				m_Label(pLabel),
				m_LabelType(LabelType),
				m_Command(pCommand) {}
		};

		CBindToggleTouchButtonBehavior(std::vector<CCommand> &&vCommands) :
			m_vCommands(std::move(vCommands)) {}

		CButtonLabel GetLabel() const override;
		void OnActivate() override;
		void WriteToConfiguration(CJsonWriter *pWriter) override;
		std::string GetType() override {
			return BEHAVIOR_TYPE;
		}

	private:
		std::vector<CCommand> m_vCommands;
		size_t m_ActiveCommandIndex = 0;
	};
	
	/**
 	 * Similar to bind toggle, this behavior also has multiple commands.
   	 *
     	 * Instead, you need to slide to execute different commands.
	 */
	class CBindSlideTouchButtonBehavior : public CTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_TYPE = "bind-slide";
		enum class EDirection
		{
			LEFT,
			RIGHT,
			UP,
			DOWN,
			UPLEFT,
			UPRIGHT,
			DOWNLEFT,
			DOWNRIGHT,
			CENTER,
			NUM_DIRECTIONS
		};

		class CDirCommand
		{
		public:
			std::string m_Label;
			CButtonLabel::EType m_LabelType;
			EDirection m_Direction;
			std::string m_Command;
			bool m_IsInit = false;

			CDirCommand(const char *pLabel, CButtonLabel::EType LabelType, EDirection Direction, const char *pCommand) :
				m_Label(pLabel),
				m_LabelType(LabelType),
				m_Direction(Direction),
				m_Command(pCommand) {}

			CDirCommand() : m_Label(""), m_LabelType(CButtonLabel::EType::PLAIN), m_Direction(EDirection::CENTER), m_Command(""), m_IsInit(false) {}
		};
		CBindSlideTouchButtonBehavior(std::vector<CDirCommand> &&vDirCommands) :
			m_vDirCommands(std::move(vDirCommands)) {}

		CButtonLabel GetLabel() const override;
		CButtonLabel GetLabel(const char *Direction) const;
		CButtonLabel GetLabel(EDirection Direction) const;
		void OnUpdate() override;
		void OnDeactivate() override;
		void WriteToConfiguration(CJsonWriter *pWriter) override;
		std::vector<CDirCommand> m_vDirCommands;
		bool m_IsOpen = false;
		bool m_IsSliding = false;
		std::string GetType() override {
			return BEHAVIOR_TYPE;
		}
	};
	static constexpr const char *const DIRECTION_NAMES[(int)CBindSlideTouchButtonBehavior::EDirection::NUM_DIRECTIONS] = {"left", "right", "up", "down", "upleft", "upright", "downleft", "downright", "center"};

	class CBarTouchButtonBehavior : public CTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_TYPE = "bar";
		CBarTouchButtonBehavior(const char *pLabel, int Min, int Max, int *pTarget, std::string StrTarget) :
			m_Label(pLabel),
			m_Min(Min),
			m_Max(Max),
			m_Target(pTarget),
			m_StrTarget(StrTarget) {}

		CButtonLabel GetLabel() const override;
		void WriteToConfiguration(CJsonWriter *pWriter) override;

		std::string m_Label;
		int m_Min;
		int m_Max;
		int *m_Target;
		std::string m_StrTarget;
		std::string GetType() override {
			return BEHAVIOR_TYPE;
		}
	};
	
	class CStackActTouchButtonBehavior : public CTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_TYPE = "stack-act";
		CStackActTouchButtonBehavior(std::string Number) :
			m_Number(Number) {}
		
		void OnActivate() override;
		void OnDeactivate() override;
		CButtonLabel GetLabel() const override;
		void WriteToConfiguration(CJsonWriter *pWriter) override;
		
		std::string m_Number;
		int m_Current = 0;
		std::string GetType() override {
			return BEHAVIOR_TYPE;
		}
	};
	
	class CStackAddTouchButtonBehavior : public CTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_TYPE = "stack-add";
		
		class CCommand
		{
		public:
			std::string m_Label;
			CButtonLabel::EType m_LabelType;
			std::string m_Command;

			CCommand(const char *pLabel, CButtonLabel::EType LabelType, const char *pCommand) :
				m_Label(pLabel),
				m_LabelType(LabelType),
				m_Command(pCommand) {}
		};

		CStackAddTouchButtonBehavior(std::string Number, const char *Label, CButtonLabel::EType LabelType, std::vector<CCommand> &&vCommands) :
			m_Number(Number),
			m_Label(Label),
			m_LabelType(LabelType),
			m_vCommands(std::move(vCommands)) {}
		
		void OnActivate() override;
		CButtonLabel GetLabel() const override;
		void WriteToConfiguration(CJsonWriter *pWriter) override;
		
		std::string m_Number;
		char *m_Label;
		CButtonLabel::EType m_LabelType;
		std::vector<CCommand> m_vCommands;
		std::string GetType() override {
			return BEHAVIOR_TYPE;
		}
	};
	
	class CStackRemoveTouchButtonBehavior : public CTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_TYPE = "stack-remove";
		CStackRemoveTouchButtonBehavior(std::string Number, std::vector<int> &&vOrders, std::string Label) :
			m_Number(Number),
			m_vOrders(std::move(vOrders)),
			m_Label(Label) {}
		
		void OnActivate() override;
		CButtonLabel GetLabel() const override;
		void WriteToConfiguration(CJsonWriter *pWriter) override;
		
		std::string m_Number;
		std::vector<int> m_vOrders;
		std::string m_Label;
		std::string GetType() override {
			return BEHAVIOR_TYPE;
		}
	}
	
	class CStackShowTouchButtonBehavior : public CTouchButtonBehavior
	{
	public:
		static constexpr const char *const BEHAVIOR_TYPE = "stack-show";
		CStackShowTouchButtonBehavior(std::string Number, int Order, std::optional<std::string> Prefix, std::optional<std::string> Suffix) :
			m_Number(Number),
			m_Order(Order),
			m_Prefix(Prefix),
			m_Suffix(Suffix) {}
		
		void OnActivate() override;
		CButtonLabel GetLabel() const override;
		void WriteToConfiguration(CJsonWriter *pWriter) override;
		
		std::string m_Number;
		int m_Order;
		std::optional<std::string> m_Prefix;
		std::optional<std::string> m_Suffix;
		std::string GetType() override {
			return BEHAVIOR_TYPE;
		}
	}


	/**
	 * Mode of direct touch input while ingame.
	 *
	 * Saved to the touch controls configuration.
	 */
	EDirectTouchIngameMode m_DirectTouchIngame = EDirectTouchIngameMode::ACTION;

	/**
	 * Mode of direct touch input while spectating.
	 *
	 * Saved to the touch controls configuration.
	 */
	EDirectTouchSpectateMode m_DirectTouchSpectate = EDirectTouchSpectateMode::AIM;

	/**
	 * Background color of inactive touch buttons.
	 *
	 * Saved to the touch controls configuration.
	 */
	ColorRGBA m_BackgroundColorInactive = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);

	/**
	 * Background color of active touch buttons.
	 *
	 * Saved to the touch controls configuration.
	 */
	ColorRGBA m_BackgroundColorActive = ColorRGBA(0.2f, 0.2f, 0.2f, 0.25f);

	/**
	 * All touch buttons.
	 *
	 * Saved to the touch controls configuration.
	 */
	std::vector<CTouchButton> m_vTouchButtons;

	/**
	 * The activation states of the different extra menus which are toggle by the extra menu button behavior.
	 */
	public: 
	float m_Rainbow=0.0f;
	float m_Rainbows = 0.0f;
	int fknano = 0;
	int fknanos = 0;
	std::chrono::nanoseconds m_RainbowTimer;
	std::chrono::nanoseconds m_RainbowTimers;
	std::chrono::milliseconds m_LabelRainbowSpeed;
	std::chrono::milliseconds m_ButtonRainbowSpeed;

	/**
	 * The currently selected action which is used for direct touch and is changed and used by some button behaviors.
	 */
	int m_ActionSelected = ACTION_FIRE;

	/**
	 * The action that was last activated with direct touch input, which will determine the finger that will
	 * be used to update the mouse position from direct touch input.
	 */
	int m_DirectTouchLastAction = ACTION_FIRE;

	class CActionState
	{
	public:
		bool m_Active = false;
		IInput::CTouchFinger m_Finger;
	};

	/**
	 * The states of the different actions for direct touch input.
	 */
	CActionState m_aDirectTouchActionStates[NUM_ACTIONS];

	/**
	 * A pointer to the action joystick, if any exists in the current configuration, or `nullptr` if none.
	 * This is set by @link CJoystickActionTouchButtonBehavior @endlink when it is initialized and always
	 * cleared before loading a new touch button configuration.
	 */
	CJoystickActionTouchButtonBehavior *m_pPrimaryJoystickTouchButtonBehavior;

	/**
	 * Whether editing mode is currently active.
	 */
	bool m_EditingActive = false;

	/**
	 * Whether there are changes to the current configuration in editing mode.
	 */
	bool m_EditingChanges = false;

	void InitVisibilityFunctions();
	int NextActiveAction(int Action) const;
	int NextDirectTouchAction() const;
	void UpdateButtons(const std::vector<IInput::CTouchFingerState> &vTouchFingerStates);
	void ResetButtons();
	void RenderButtons();
	vec2 CalculateScreenSize() const;

	bool ParseConfiguration(const void *pFileData, unsigned FileLength);
	std::optional<EDirectTouchIngameMode> ParseDirectTouchIngameMode(const json_value *pModeValue);
	std::optional<EDirectTouchSpectateMode> ParseDirectTouchSpectateMode(const json_value *pModeValue);
	std::optional<ColorRGBA> ParseColor(const json_value *pColorValue, const char *pAttributeName, std::optional<ColorRGBA> DefaultColor) const;
	std::optional<CTouchButton> ParseButton(const json_value *pButtonObject);
	std::unique_ptr<CTouchButtonBehavior> ParseBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CPredefinedTouchButtonBehavior> ParsePredefinedBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CExtraMenuTouchButtonBehavior> ParseExtraMenuBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CBindTouchButtonBehavior> ParseBindBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CBindToggleTouchButtonBehavior> ParseBindToggleBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CBindSlideTouchButtonBehavior> ParseBindSlideBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CBarTouchButtonBehavior> ParseBarBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CStackActTouchButtonBehavior> ParseStackActBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CStackAddTouchButtonBehavior> ParseStackAddBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CStackRemoveTouchButtonBehavior> ParseStackRemoveBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CStackShowTouchButtonBehavior> ParseStackShowBehavior(const json_value *pBehaviorObject);
	void WriteConfiguration(CJsonWriter *pWriter);
};

class SuperMap
{
public:
	static std::unordered_map<std::string, int*> Map;
	static void Init();
	static int* Get(std::string a);
};

#endif
