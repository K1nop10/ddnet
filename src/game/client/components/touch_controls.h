#ifndef GAME_CLIENT_COMPONENTS_TOUCH_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_TOUCH_CONTROLS_H

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
#include <map>

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
		EXTRA_MENU_1,
		EXTRA_MENU_2,
		EXTRA_MENU_3,
		EXTRA_MENU_4,
		EXTRA_MENU_5,
		EXTRA_MENU_6,
		EXTRA_MENU_7,
        EXTRA_MENU_8,
        EXTRA_MENU_9,
        EXTRA_MENU_10,
        EXTRA_MENU_11,
        EXTRA_MENU_12,
        EXTRA_MENU_13,
        EXTRA_MENU_14,
        EXTRA_MENU_15,
        EXTRA_MENU_16,
        EXTRA_MENU_17,
        EXTRA_MENU_18,
        EXTRA_MENU_19,
        EXTRA_MENU_20,
        EXTRA_MENU_21,
        EXTRA_MENU_22,
        EXTRA_MENU_23,
        EXTRA_MENU_24,
        EXTRA_MENU_25,
        EXTRA_MENU_26,
        EXTRA_MENU_27,
        EXTRA_MENU_28,
        EXTRA_MENU_29,
        EXTRA_MENU_30,
        EXTRA_MENU_31,
        EXTRA_MENU_32,
        EXTRA_MENU_33,
        EXTRA_MENU_34,
        EXTRA_MENU_35,
        EXTRA_MENU_36,
        EXTRA_MENU_37,
        EXTRA_MENU_38,
        EXTRA_MENU_39,
        EXTRA_MENU_40,
        EXTRA_MENU_41,
        EXTRA_MENU_42,
        EXTRA_MENU_43,
        EXTRA_MENU_44,
        EXTRA_MENU_45,
        EXTRA_MENU_46,
        EXTRA_MENU_47,
        EXTRA_MENU_48,
        EXTRA_MENU_49,
        EXTRA_MENU_50,
        EXTRA_MENU_51,
        EXTRA_MENU_52,
        EXTRA_MENU_53,
        EXTRA_MENU_54,
        EXTRA_MENU_55,
        EXTRA_MENU_56,
        EXTRA_MENU_57,
        EXTRA_MENU_58,
        EXTRA_MENU_59,
        EXTRA_MENU_60,
        EXTRA_MENU_61,
        EXTRA_MENU_62,
        EXTRA_MENU_63,
        EXTRA_MENU_64,
        EXTRA_MENU_65,
        EXTRA_MENU_66,
        EXTRA_MENU_67,
        EXTRA_MENU_68,
        EXTRA_MENU_69,
        EXTRA_MENU_70,
        EXTRA_MENU_71,
        EXTRA_MENU_72,
        EXTRA_MENU_73,
        EXTRA_MENU_74,
        EXTRA_MENU_75,
        EXTRA_MENU_76,
        EXTRA_MENU_77,
        EXTRA_MENU_78,
        EXTRA_MENU_79,
        EXTRA_MENU_80,
        EXTRA_MENU_81,
        EXTRA_MENU_82,
        EXTRA_MENU_83,
        EXTRA_MENU_84,
        EXTRA_MENU_85,
        EXTRA_MENU_86,
        EXTRA_MENU_87,
        EXTRA_MENU_88,
        EXTRA_MENU_89,
        EXTRA_MENU_90,
        EXTRA_MENU_91,
        EXTRA_MENU_92,
        EXTRA_MENU_93,
        EXTRA_MENU_94,
        EXTRA_MENU_95,
        EXTRA_MENU_96,
        EXTRA_MENU_97,
        EXTRA_MENU_98,
        EXTRA_MENU_99,
        EXTRA_MENU_100,
        EXTRA_MENU_101,
        EXTRA_MENU_102,
        EXTRA_MENU_103,
        EXTRA_MENU_104,
        EXTRA_MENU_105,
        EXTRA_MENU_106,
        EXTRA_MENU_107,
        EXTRA_MENU_108,
        EXTRA_MENU_109,
        EXTRA_MENU_110,
        EXTRA_MENU_111,
        EXTRA_MENU_112,
        EXTRA_MENU_113,
        EXTRA_MENU_114,
        EXTRA_MENU_115,
        EXTRA_MENU_116,
        EXTRA_MENU_117,
        EXTRA_MENU_118,
        EXTRA_MENU_119,
        EXTRA_MENU_120,
        EXTRA_MENU_121,
        EXTRA_MENU_122,
        EXTRA_MENU_123,
        EXTRA_MENU_124,
        EXTRA_MENU_125,
        EXTRA_MENU_126,
        EXTRA_MENU_127,
        EXTRA_MENU_128,
        EXTRA_MENU_129,
        EXTRA_MENU_130,
        EXTRA_MENU_131,
        EXTRA_MENU_132,
        EXTRA_MENU_133,
        EXTRA_MENU_134,
        EXTRA_MENU_135,
        EXTRA_MENU_136,
        EXTRA_MENU_137,
        EXTRA_MENU_138,
        EXTRA_MENU_139,
        EXTRA_MENU_140,
        EXTRA_MENU_141,
        EXTRA_MENU_142,
        EXTRA_MENU_143,
        EXTRA_MENU_144,
        EXTRA_MENU_145,
        EXTRA_MENU_146,
        EXTRA_MENU_147,
        EXTRA_MENU_148,
        EXTRA_MENU_149,
        EXTRA_MENU_150,
        EXTRA_MENU_151,
        EXTRA_MENU_152,
        EXTRA_MENU_153,
        EXTRA_MENU_154,
        EXTRA_MENU_155,
        EXTRA_MENU_156,
        EXTRA_MENU_157,
        EXTRA_MENU_158,
        EXTRA_MENU_159,
        EXTRA_MENU_160,
        EXTRA_MENU_161,
        EXTRA_MENU_162,
        EXTRA_MENU_163,
        EXTRA_MENU_164,
        EXTRA_MENU_165,
        EXTRA_MENU_166,
        EXTRA_MENU_167,
        EXTRA_MENU_168,
        EXTRA_MENU_169,
        EXTRA_MENU_170,
        EXTRA_MENU_171,
        EXTRA_MENU_172,
        EXTRA_MENU_173,
        EXTRA_MENU_174,
        EXTRA_MENU_175,
        EXTRA_MENU_176,
        EXTRA_MENU_177,
        EXTRA_MENU_178,
        EXTRA_MENU_179,
        EXTRA_MENU_180,
        EXTRA_MENU_181,
        EXTRA_MENU_182,
        EXTRA_MENU_183,
        EXTRA_MENU_184,
        EXTRA_MENU_185,
        EXTRA_MENU_186,
        EXTRA_MENU_187,
        EXTRA_MENU_188,
        EXTRA_MENU_189,
        EXTRA_MENU_190,
        EXTRA_MENU_191,
        EXTRA_MENU_192,
        EXTRA_MENU_193,
        EXTRA_MENU_194,
        EXTRA_MENU_195,
        EXTRA_MENU_196,
        EXTRA_MENU_197,
        EXTRA_MENU_198,
        EXTRA_MENU_199,
        EXTRA_MENU_200,
        EXTRA_MENU_201,
        EXTRA_MENU_202,
        EXTRA_MENU_203,
        EXTRA_MENU_204,
        EXTRA_MENU_205,
        EXTRA_MENU_206,
        EXTRA_MENU_207,
        EXTRA_MENU_208,
        EXTRA_MENU_209,
        EXTRA_MENU_210,
        EXTRA_MENU_211,
        EXTRA_MENU_212,
        EXTRA_MENU_213,
        EXTRA_MENU_214,
        EXTRA_MENU_215,
        EXTRA_MENU_216,
        EXTRA_MENU_217,
        EXTRA_MENU_218,
        EXTRA_MENU_219,
        EXTRA_MENU_220,
        EXTRA_MENU_221,
        EXTRA_MENU_222,
        EXTRA_MENU_223,
        EXTRA_MENU_224,
        EXTRA_MENU_225,
        EXTRA_MENU_226,
        EXTRA_MENU_227,
        EXTRA_MENU_228,
        EXTRA_MENU_229,
        EXTRA_MENU_230,
        EXTRA_MENU_231,
        EXTRA_MENU_232,
        EXTRA_MENU_233,
        EXTRA_MENU_234,
        EXTRA_MENU_235,
        EXTRA_MENU_236,
        EXTRA_MENU_237,
        EXTRA_MENU_238,
        EXTRA_MENU_239,
        EXTRA_MENU_240,
        EXTRA_MENU_241,
        EXTRA_MENU_242,
        EXTRA_MENU_243,
        EXTRA_MENU_244,
        EXTRA_MENU_245,
        EXTRA_MENU_246,
        EXTRA_MENU_247,
        EXTRA_MENU_248,
        EXTRA_MENU_249,
        EXTRA_MENU_250,
        EXTRA_MENU_251,
        EXTRA_MENU_252,
        EXTRA_MENU_253,
        EXTRA_MENU_254,
        EXTRA_MENU_255,
        EXTRA_MENU_256,
        EXTRA_MENU_257,
        EXTRA_MENU_258,
        EXTRA_MENU_259,
        EXTRA_MENU_260,
        EXTRA_MENU_261,
        EXTRA_MENU_262,
        EXTRA_MENU_263,
        EXTRA_MENU_264,
        EXTRA_MENU_265,
        EXTRA_MENU_266,
        EXTRA_MENU_267,
        EXTRA_MENU_268,
        EXTRA_MENU_269,
        EXTRA_MENU_270,
        EXTRA_MENU_271,
        EXTRA_MENU_272,
        EXTRA_MENU_273,
        EXTRA_MENU_274,
        EXTRA_MENU_275,
        EXTRA_MENU_276,
        EXTRA_MENU_277,
        EXTRA_MENU_278,
        EXTRA_MENU_279,
        EXTRA_MENU_280,
        EXTRA_MENU_281,
        EXTRA_MENU_282,
        EXTRA_MENU_283,
        EXTRA_MENU_284,
        EXTRA_MENU_285,
        EXTRA_MENU_286,
        EXTRA_MENU_287,
        EXTRA_MENU_288,
        EXTRA_MENU_289,
        EXTRA_MENU_290,
        EXTRA_MENU_291,
        EXTRA_MENU_292,
        EXTRA_MENU_293,
        EXTRA_MENU_294,
        EXTRA_MENU_295,
        EXTRA_MENU_296,
        EXTRA_MENU_297,
        EXTRA_MENU_298,
        EXTRA_MENU_299,
        EXTRA_MENU_300,
        EXTRA_MENU_301,
        EXTRA_MENU_302,
        EXTRA_MENU_303,
        EXTRA_MENU_304,
        EXTRA_MENU_305,
        EXTRA_MENU_306,
        EXTRA_MENU_307,
        EXTRA_MENU_308,
        EXTRA_MENU_309,
        EXTRA_MENU_310,
        EXTRA_MENU_311,
        EXTRA_MENU_312,
        EXTRA_MENU_313,
        EXTRA_MENU_314,
        EXTRA_MENU_315,
        EXTRA_MENU_316,
        EXTRA_MENU_317,
        EXTRA_MENU_318,
        EXTRA_MENU_319,
        EXTRA_MENU_320,
        EXTRA_MENU_321,
        EXTRA_MENU_322,
        EXTRA_MENU_323,
        EXTRA_MENU_324,
        EXTRA_MENU_325,
        EXTRA_MENU_326,
        EXTRA_MENU_327,
        EXTRA_MENU_328,
        EXTRA_MENU_329,
        EXTRA_MENU_330,
        EXTRA_MENU_331,
        EXTRA_MENU_332,
        EXTRA_MENU_333,
        EXTRA_MENU_334,
        EXTRA_MENU_335,
        EXTRA_MENU_336,
        EXTRA_MENU_337,
        EXTRA_MENU_338,
        EXTRA_MENU_339,
        EXTRA_MENU_340,
        EXTRA_MENU_341,
        EXTRA_MENU_342,
        EXTRA_MENU_343,
        EXTRA_MENU_344,
        EXTRA_MENU_345,
        EXTRA_MENU_346,
        EXTRA_MENU_347,
        EXTRA_MENU_348,
        EXTRA_MENU_349,
        EXTRA_MENU_350,
        EXTRA_MENU_351,
        EXTRA_MENU_352,
        EXTRA_MENU_353,
        EXTRA_MENU_354,
        EXTRA_MENU_355,
        EXTRA_MENU_356,
        EXTRA_MENU_357,
        EXTRA_MENU_358,
        EXTRA_MENU_359,
        EXTRA_MENU_360,
        EXTRA_MENU_361,
        EXTRA_MENU_362,
        EXTRA_MENU_363,
        EXTRA_MENU_364,
        EXTRA_MENU_365,
        EXTRA_MENU_366,
        EXTRA_MENU_367,
        EXTRA_MENU_368,
        EXTRA_MENU_369,
        EXTRA_MENU_370,
        EXTRA_MENU_371,
        EXTRA_MENU_372,
        EXTRA_MENU_373,
        EXTRA_MENU_374,
        EXTRA_MENU_375,
        EXTRA_MENU_376,
        EXTRA_MENU_377,
        EXTRA_MENU_378,
        EXTRA_MENU_379,
        EXTRA_MENU_380,
        EXTRA_MENU_381,
        EXTRA_MENU_382,
        EXTRA_MENU_383,
        EXTRA_MENU_384,
        EXTRA_MENU_385,
        EXTRA_MENU_386,
        EXTRA_MENU_387,
        EXTRA_MENU_388,
        EXTRA_MENU_389,
        EXTRA_MENU_390,
        EXTRA_MENU_391,
        EXTRA_MENU_392,
        EXTRA_MENU_393,
        EXTRA_MENU_394,
        EXTRA_MENU_395,
        EXTRA_MENU_396,
        EXTRA_MENU_397,
        EXTRA_MENU_398,
        EXTRA_MENU_399,
        EXTRA_MENU_400,
        EXTRA_MENU_401,
        EXTRA_MENU_402,
        EXTRA_MENU_403,
        EXTRA_MENU_404,
        EXTRA_MENU_405,
        EXTRA_MENU_406,
        EXTRA_MENU_407,
        EXTRA_MENU_408,
        EXTRA_MENU_409,
        EXTRA_MENU_410,
        EXTRA_MENU_411,
        EXTRA_MENU_412,
        EXTRA_MENU_413,
        EXTRA_MENU_414,
        EXTRA_MENU_415,
        EXTRA_MENU_416,
        EXTRA_MENU_417,
        EXTRA_MENU_418,
        EXTRA_MENU_419,
        EXTRA_MENU_420,
        EXTRA_MENU_421,
        EXTRA_MENU_422,
        EXTRA_MENU_423,
        EXTRA_MENU_424,
        EXTRA_MENU_425,
        EXTRA_MENU_426,
        EXTRA_MENU_427,
        EXTRA_MENU_428,
        EXTRA_MENU_429,
        EXTRA_MENU_430,
        EXTRA_MENU_431,
        EXTRA_MENU_432,
        EXTRA_MENU_433,
        EXTRA_MENU_434,
        EXTRA_MENU_435,
        EXTRA_MENU_436,
        EXTRA_MENU_437,
        EXTRA_MENU_438,
        EXTRA_MENU_439,
        EXTRA_MENU_440,
        EXTRA_MENU_441,
        EXTRA_MENU_442,
        EXTRA_MENU_443,
        EXTRA_MENU_444,
        EXTRA_MENU_445,
        EXTRA_MENU_446,
        EXTRA_MENU_447,
        EXTRA_MENU_448,
        EXTRA_MENU_449,
        EXTRA_MENU_450,
        EXTRA_MENU_451,
        EXTRA_MENU_452,
        EXTRA_MENU_453,
        EXTRA_MENU_454,
        EXTRA_MENU_455,
        EXTRA_MENU_456,
        EXTRA_MENU_457,
        EXTRA_MENU_458,
        EXTRA_MENU_459,
        EXTRA_MENU_460,
        EXTRA_MENU_461,
        EXTRA_MENU_462,
        EXTRA_MENU_463,
        EXTRA_MENU_464,
        EXTRA_MENU_465,
        EXTRA_MENU_466,
        EXTRA_MENU_467,
        EXTRA_MENU_468,
        EXTRA_MENU_469,
        EXTRA_MENU_470,
        EXTRA_MENU_471,
        EXTRA_MENU_472,
        EXTRA_MENU_473,
        EXTRA_MENU_474,
        EXTRA_MENU_475,
        EXTRA_MENU_476,
        EXTRA_MENU_477,
        EXTRA_MENU_478,
        EXTRA_MENU_479,
        EXTRA_MENU_480,
        EXTRA_MENU_481,
        EXTRA_MENU_482,
        EXTRA_MENU_483,
        EXTRA_MENU_484,
        EXTRA_MENU_485,
        EXTRA_MENU_486,
        EXTRA_MENU_487,
        EXTRA_MENU_488,
        EXTRA_MENU_489,
        EXTRA_MENU_490,
        EXTRA_MENU_491,
        EXTRA_MENU_492,
        EXTRA_MENU_493,
        EXTRA_MENU_494,
        EXTRA_MENU_495,
        EXTRA_MENU_496,
        EXTRA_MENU_497,
        EXTRA_MENU_498,
        EXTRA_MENU_499,
        EXTRA_MENU_500,
        EXTRA_MENU_501,
        EXTRA_MENU_502,
        EXTRA_MENU_503,
        EXTRA_MENU_504,
        EXTRA_MENU_505,
        EXTRA_MENU_506,
        EXTRA_MENU_507,
        EXTRA_MENU_508,
        EXTRA_MENU_509,
        EXTRA_MENU_510,
        EXTRA_MENU_511,
        EXTRA_MENU_512,
        EXTRA_MENU_513,
        EXTRA_MENU_514,
        EXTRA_MENU_515,
        EXTRA_MENU_516,
        EXTRA_MENU_517,
        EXTRA_MENU_518,
        EXTRA_MENU_519,
        EXTRA_MENU_520,
        EXTRA_MENU_521,
        EXTRA_MENU_522,
        EXTRA_MENU_523,
        EXTRA_MENU_524,
        EXTRA_MENU_525,
        EXTRA_MENU_526,
        EXTRA_MENU_527,
        EXTRA_MENU_528,
        EXTRA_MENU_529,
        EXTRA_MENU_530,
        EXTRA_MENU_531,
        EXTRA_MENU_532,
        EXTRA_MENU_533,
        EXTRA_MENU_534,
        EXTRA_MENU_535,
        EXTRA_MENU_536,
        EXTRA_MENU_537,
        EXTRA_MENU_538,
        EXTRA_MENU_539,
        EXTRA_MENU_540,
        EXTRA_MENU_541,
        EXTRA_MENU_542,
        EXTRA_MENU_543,
        EXTRA_MENU_544,
        EXTRA_MENU_545,
        EXTRA_MENU_546,
        EXTRA_MENU_547,
        EXTRA_MENU_548,
        EXTRA_MENU_549,
        EXTRA_MENU_550,
        EXTRA_MENU_551,
        EXTRA_MENU_552,
        EXTRA_MENU_553,
        EXTRA_MENU_554,
        EXTRA_MENU_555,
        EXTRA_MENU_556,
        EXTRA_MENU_557,
        EXTRA_MENU_558,
        EXTRA_MENU_559,
        EXTRA_MENU_560,
        EXTRA_MENU_561,
        EXTRA_MENU_562,
        EXTRA_MENU_563,
        EXTRA_MENU_564,
        EXTRA_MENU_565,
        EXTRA_MENU_566,
        EXTRA_MENU_567,
        EXTRA_MENU_568,
        EXTRA_MENU_569,
        EXTRA_MENU_570,
        EXTRA_MENU_571,
        EXTRA_MENU_572,
        EXTRA_MENU_573,
        EXTRA_MENU_574,
        EXTRA_MENU_575,
        EXTRA_MENU_576,
        EXTRA_MENU_577,
        EXTRA_MENU_578,
        EXTRA_MENU_579,
        EXTRA_MENU_580,
        EXTRA_MENU_581,
        EXTRA_MENU_582,
        EXTRA_MENU_583,
        EXTRA_MENU_584,
        EXTRA_MENU_585,
        EXTRA_MENU_586,
        EXTRA_MENU_587,
        EXTRA_MENU_588,
        EXTRA_MENU_589,
        EXTRA_MENU_590,
        EXTRA_MENU_591,
        EXTRA_MENU_592,
        EXTRA_MENU_593,
        EXTRA_MENU_594,
        EXTRA_MENU_595,
        EXTRA_MENU_596,
        EXTRA_MENU_597,
        EXTRA_MENU_598,
        EXTRA_MENU_599,
        EXTRA_MENU_600,
        EXTRA_MENU_601,
        EXTRA_MENU_602,
        EXTRA_MENU_603,
        EXTRA_MENU_604,
        EXTRA_MENU_605,
        EXTRA_MENU_606,
        EXTRA_MENU_607,
        EXTRA_MENU_608,
        EXTRA_MENU_609,
        EXTRA_MENU_610,
        EXTRA_MENU_611,
        EXTRA_MENU_612,
        EXTRA_MENU_613,
        EXTRA_MENU_614,
        EXTRA_MENU_615,
        EXTRA_MENU_616,
        EXTRA_MENU_617,
        EXTRA_MENU_618,
        EXTRA_MENU_619,
        EXTRA_MENU_620,
        EXTRA_MENU_621,
        EXTRA_MENU_622,
        EXTRA_MENU_623,
        EXTRA_MENU_624,
        EXTRA_MENU_625,
        EXTRA_MENU_626,
        EXTRA_MENU_627,
        EXTRA_MENU_628,
        EXTRA_MENU_629,
        EXTRA_MENU_630,
        EXTRA_MENU_631,
        EXTRA_MENU_632,
        EXTRA_MENU_633,
        EXTRA_MENU_634,
        EXTRA_MENU_635,
        EXTRA_MENU_636,
        EXTRA_MENU_637,
        EXTRA_MENU_638,
        EXTRA_MENU_639,
        EXTRA_MENU_640,
        EXTRA_MENU_641,
        EXTRA_MENU_642,
        EXTRA_MENU_643,
        EXTRA_MENU_644,
        EXTRA_MENU_645,
        EXTRA_MENU_646,
        EXTRA_MENU_647,
        EXTRA_MENU_648,
        EXTRA_MENU_649,
        EXTRA_MENU_650,
        EXTRA_MENU_651,
        EXTRA_MENU_652,
        EXTRA_MENU_653,
        EXTRA_MENU_654,
        EXTRA_MENU_655,
        EXTRA_MENU_656,
        EXTRA_MENU_657,
        EXTRA_MENU_658,
        EXTRA_MENU_659,
        EXTRA_MENU_660,
        EXTRA_MENU_661,
        EXTRA_MENU_662,
        EXTRA_MENU_663,
        EXTRA_MENU_664,
        EXTRA_MENU_665,
        EXTRA_MENU_666,
        EXTRA_MENU_667,
        EXTRA_MENU_668,
        EXTRA_MENU_669,
        EXTRA_MENU_670,
        EXTRA_MENU_671,
        EXTRA_MENU_672,
        EXTRA_MENU_673,
        EXTRA_MENU_674,
        EXTRA_MENU_675,
        EXTRA_MENU_676,
        EXTRA_MENU_677,
        EXTRA_MENU_678,
        EXTRA_MENU_679,
        EXTRA_MENU_680,
        EXTRA_MENU_681,
        EXTRA_MENU_682,
        EXTRA_MENU_683,
        EXTRA_MENU_684,
        EXTRA_MENU_685,
        EXTRA_MENU_686,
        EXTRA_MENU_687,
        EXTRA_MENU_688,
        EXTRA_MENU_689,
        EXTRA_MENU_690,
        EXTRA_MENU_691,
        EXTRA_MENU_692,
        EXTRA_MENU_693,
        EXTRA_MENU_694,
        EXTRA_MENU_695,
        EXTRA_MENU_696,
        EXTRA_MENU_697,
        EXTRA_MENU_698,
        EXTRA_MENU_699,
        EXTRA_MENU_700,
        EXTRA_MENU_701,
        EXTRA_MENU_702,
        EXTRA_MENU_703,
        EXTRA_MENU_704,
        EXTRA_MENU_705,
        EXTRA_MENU_706,
        EXTRA_MENU_707,
        EXTRA_MENU_708,
        EXTRA_MENU_709,
        EXTRA_MENU_710,
        EXTRA_MENU_711,
        EXTRA_MENU_712,
        EXTRA_MENU_713,
        EXTRA_MENU_714,
        EXTRA_MENU_715,
        EXTRA_MENU_716,
        EXTRA_MENU_717,
        EXTRA_MENU_718,
        EXTRA_MENU_719,
        EXTRA_MENU_720,
        EXTRA_MENU_721,
        EXTRA_MENU_722,
        EXTRA_MENU_723,
        EXTRA_MENU_724,
        EXTRA_MENU_725,
        EXTRA_MENU_726,
        EXTRA_MENU_727,
        EXTRA_MENU_728,
        EXTRA_MENU_729,
        EXTRA_MENU_730,
        EXTRA_MENU_731,
        EXTRA_MENU_732,
        EXTRA_MENU_733,
        EXTRA_MENU_734,
        EXTRA_MENU_735,
        EXTRA_MENU_736,
        EXTRA_MENU_737,
        EXTRA_MENU_738,
        EXTRA_MENU_739,
        EXTRA_MENU_740,
        EXTRA_MENU_741,
        EXTRA_MENU_742,
        EXTRA_MENU_743,
        EXTRA_MENU_744,
        EXTRA_MENU_745,
        EXTRA_MENU_746,
        EXTRA_MENU_747,
        EXTRA_MENU_748,
        EXTRA_MENU_749,
        EXTRA_MENU_750,
        EXTRA_MENU_751,
        EXTRA_MENU_752,
        EXTRA_MENU_753,
        EXTRA_MENU_754,
        EXTRA_MENU_755,
        EXTRA_MENU_756,
        EXTRA_MENU_757,
        EXTRA_MENU_758,
        EXTRA_MENU_759,
        EXTRA_MENU_760,
        EXTRA_MENU_761,
        EXTRA_MENU_762,
        EXTRA_MENU_763,
        EXTRA_MENU_764,
        EXTRA_MENU_765,
        EXTRA_MENU_766,
        EXTRA_MENU_767,
        EXTRA_MENU_768,
        EXTRA_MENU_769,
        EXTRA_MENU_770,
        EXTRA_MENU_771,
        EXTRA_MENU_772,
        EXTRA_MENU_773,
        EXTRA_MENU_774,
        EXTRA_MENU_775,
        EXTRA_MENU_776,
        EXTRA_MENU_777,
        EXTRA_MENU_778,
        EXTRA_MENU_779,
        EXTRA_MENU_780,
        EXTRA_MENU_781,
        EXTRA_MENU_782,
        EXTRA_MENU_783,
        EXTRA_MENU_784,
        EXTRA_MENU_785,
        EXTRA_MENU_786,
        EXTRA_MENU_787,
        EXTRA_MENU_788,
        EXTRA_MENU_789,
        EXTRA_MENU_790,
        EXTRA_MENU_791,
        EXTRA_MENU_792,
        EXTRA_MENU_793,
        EXTRA_MENU_794,
        EXTRA_MENU_795,
        EXTRA_MENU_796,
        EXTRA_MENU_797,
        EXTRA_MENU_798,
        EXTRA_MENU_799,
        EXTRA_MENU_800,
        EXTRA_MENU_801,
        EXTRA_MENU_802,
        EXTRA_MENU_803,
        EXTRA_MENU_804,
        EXTRA_MENU_805,
        EXTRA_MENU_806,
        EXTRA_MENU_807,
        EXTRA_MENU_808,
        EXTRA_MENU_809,
        EXTRA_MENU_810,
        EXTRA_MENU_811,
        EXTRA_MENU_812,
        EXTRA_MENU_813,
        EXTRA_MENU_814,
        EXTRA_MENU_815,
        EXTRA_MENU_816,
        EXTRA_MENU_817,
        EXTRA_MENU_818,
        EXTRA_MENU_819,
        EXTRA_MENU_820,
        EXTRA_MENU_821,
        EXTRA_MENU_822,
        EXTRA_MENU_823,
        EXTRA_MENU_824,
        EXTRA_MENU_825,
        EXTRA_MENU_826,
        EXTRA_MENU_827,
        EXTRA_MENU_828,
        EXTRA_MENU_829,
        EXTRA_MENU_830,
        EXTRA_MENU_831,
        EXTRA_MENU_832,
        EXTRA_MENU_833,
        EXTRA_MENU_834,
        EXTRA_MENU_835,
        EXTRA_MENU_836,
        EXTRA_MENU_837,
        EXTRA_MENU_838,
        EXTRA_MENU_839,
        EXTRA_MENU_840,
        EXTRA_MENU_841,
        EXTRA_MENU_842,
        EXTRA_MENU_843,
        EXTRA_MENU_844,
        EXTRA_MENU_845,
        EXTRA_MENU_846,
        EXTRA_MENU_847,
        EXTRA_MENU_848,
        EXTRA_MENU_849,
        EXTRA_MENU_850,
        EXTRA_MENU_851,
        EXTRA_MENU_852,
        EXTRA_MENU_853,
        EXTRA_MENU_854,
        EXTRA_MENU_855,
        EXTRA_MENU_856,
        EXTRA_MENU_857,
        EXTRA_MENU_858,
        EXTRA_MENU_859,
        EXTRA_MENU_860,
        EXTRA_MENU_861,
        EXTRA_MENU_862,
        EXTRA_MENU_863,
        EXTRA_MENU_864,
        EXTRA_MENU_865,
        EXTRA_MENU_866,
        EXTRA_MENU_867,
        EXTRA_MENU_868,
        EXTRA_MENU_869,
        EXTRA_MENU_870,
        EXTRA_MENU_871,
        EXTRA_MENU_872,
        EXTRA_MENU_873,
        EXTRA_MENU_874,
        EXTRA_MENU_875,
        EXTRA_MENU_876,
        EXTRA_MENU_877,
        EXTRA_MENU_878,
        EXTRA_MENU_879,
        EXTRA_MENU_880,
        EXTRA_MENU_881,
        EXTRA_MENU_882,
        EXTRA_MENU_883,
        EXTRA_MENU_884,
        EXTRA_MENU_885,
        EXTRA_MENU_886,
        EXTRA_MENU_887,
        EXTRA_MENU_888,
        EXTRA_MENU_889,
        EXTRA_MENU_890,
        EXTRA_MENU_891,
        EXTRA_MENU_892,
        EXTRA_MENU_893,
        EXTRA_MENU_894,
        EXTRA_MENU_895,
        EXTRA_MENU_896,
        EXTRA_MENU_897,
        EXTRA_MENU_898,
        EXTRA_MENU_899,
        EXTRA_MENU_900,
        EXTRA_MENU_901,
        EXTRA_MENU_902,
        EXTRA_MENU_903,
        EXTRA_MENU_904,
        EXTRA_MENU_905,
        EXTRA_MENU_906,
        EXTRA_MENU_907,
        EXTRA_MENU_908,
        EXTRA_MENU_909,
        EXTRA_MENU_910,
        EXTRA_MENU_911,
        EXTRA_MENU_912,
        EXTRA_MENU_913,
        EXTRA_MENU_914,
        EXTRA_MENU_915,
        EXTRA_MENU_916,
        EXTRA_MENU_917,
        EXTRA_MENU_918,
        EXTRA_MENU_919,
        EXTRA_MENU_920,
        EXTRA_MENU_921,
        EXTRA_MENU_922,
        EXTRA_MENU_923,
        EXTRA_MENU_924,
        EXTRA_MENU_925,
        EXTRA_MENU_926,
        EXTRA_MENU_927,
        EXTRA_MENU_928,
        EXTRA_MENU_929,
        EXTRA_MENU_930,
        EXTRA_MENU_931,
        EXTRA_MENU_932,
        EXTRA_MENU_933,
        EXTRA_MENU_934,
        EXTRA_MENU_935,
        EXTRA_MENU_936,
        EXTRA_MENU_937,
        EXTRA_MENU_938,
        EXTRA_MENU_939,
        EXTRA_MENU_940,
        EXTRA_MENU_941,
        EXTRA_MENU_942,
        EXTRA_MENU_943,
        EXTRA_MENU_944,
        EXTRA_MENU_945,
        EXTRA_MENU_946,
        EXTRA_MENU_947,
        EXTRA_MENU_948,
        EXTRA_MENU_949,
        EXTRA_MENU_950,
        EXTRA_MENU_951,
        EXTRA_MENU_952,
        EXTRA_MENU_953,
        EXTRA_MENU_954,
        EXTRA_MENU_955,
        EXTRA_MENU_956,
        EXTRA_MENU_957,
        EXTRA_MENU_958,
        EXTRA_MENU_959,
        EXTRA_MENU_960,
        EXTRA_MENU_961,
        EXTRA_MENU_962,
        EXTRA_MENU_963,
        EXTRA_MENU_964,
        EXTRA_MENU_965,
        EXTRA_MENU_966,
        EXTRA_MENU_967,
        EXTRA_MENU_968,
        EXTRA_MENU_969,
        EXTRA_MENU_970,
        EXTRA_MENU_971,
        EXTRA_MENU_972,
        EXTRA_MENU_973,
        EXTRA_MENU_974,
        EXTRA_MENU_975,
        EXTRA_MENU_976,
        EXTRA_MENU_977,
        EXTRA_MENU_978,
        EXTRA_MENU_979,
        EXTRA_MENU_980,
        EXTRA_MENU_981,
        EXTRA_MENU_982,
        EXTRA_MENU_983,
        EXTRA_MENU_984,
        EXTRA_MENU_985,
        EXTRA_MENU_986,
        EXTRA_MENU_987,
        EXTRA_MENU_988,
        EXTRA_MENU_989,
        EXTRA_MENU_990,
        EXTRA_MENU_991,
        EXTRA_MENU_992,
        EXTRA_MENU_993,
        EXTRA_MENU_994,
        EXTRA_MENU_995,
        EXTRA_MENU_996,
        EXTRA_MENU_997,
        EXTRA_MENU_998,
        EXTRA_MENU_999,
		NUM_VISIBILITIES
	};

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

		CExtraMenuTouchButtonBehavior(int Number);

		CButtonLabel GetLabel() const override;
		void OnDeactivate() override;
		void WriteToConfiguration(CJsonWriter *pWriter) override;

	private:
		int m_Number;
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
	};
	static constexpr const char *const DIRECTION_NAMES[(int)CBindSlideTouchButtonBehavior::EDirection::NUM_DIRECTIONS] = {"left", "right", "up", "down", "upleft", "upright", "downleft", "downright", "center"};
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
	 * All touch buttons.
	 *
	 * Saved to the touch controls configuration.
	 */
	std::vector<CTouchButton> m_vTouchButtons;

	/**
	 * The activation states of the different extra menus which are toggle by the extra menu button behavior.
	 */
	public: 
	bool m_aExtraMenuActive[(int)EButtonVisibility::EXTRA_MENU_999 - (int)EButtonVisibility::EXTRA_MENU_1 + 1] = {false};
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
	std::optional<CTouchButton> ParseButton(const json_value *pButtonObject);
	std::unique_ptr<CTouchButtonBehavior> ParseBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CPredefinedTouchButtonBehavior> ParsePredefinedBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CExtraMenuTouchButtonBehavior> ParseExtraMenuBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CBindTouchButtonBehavior> ParseBindBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CBindToggleTouchButtonBehavior> ParseBindToggleBehavior(const json_value *pBehaviorObject);
	std::unique_ptr<CBindSlideTouchButtonBehavior> ParseBindSlideBehavior(const json_value *pBehaviorObject);
	void WriteConfiguration(CJsonWriter *pWriter);
};

class SuperMap
{
public:
	static std::map<std::string, int*> Map;
	static void Init();
	static int* Get(std::string a);
};


#endif
