// Energy Sword for Skyrim SE/AE
// One-handed plasma sword (model and texture from Halo, supplied by the user for personal use).
//   - Shock damage on every hit through a contact enchantment. At startup the plugin swaps in vanilla's own
//     shock-enchantment effect, which brings the crackling blue lightning on the blade.
//   - The Halo sword is gripped by a bar across the middle, not a normal hilt. Every frame the plugin lines the
//     sword up with the player's actual fist (from the finger bones), in first and third person.
//   - Swing sounds, impact effects and keywords are copied from the vanilla Iron Sword.

namespace ES
{
	constexpr std::string_view kPluginFile = "EnergySword.esp"sv;
	constexpr RE::FormID       kWeaponID = 0x803;
	constexpr RE::FormID       kEnchantID = 0x801;
	constexpr const char*      kRootName = "EnergySword";

	struct Config
	{
		bool          alignGrip = true;
		int           style = 1;  // 1 = Halo: blades out along the fist, bar across it. 0 = bar along the fist (first version)
		float         gripX = 0.0f, gripY = 0.0f, gripZ = 0.0f;  // nudge: across the blade, along the blade, up the handle
		float         twist = 0.0f;   // degrees: spin around the blade direction (which way the prongs point)
		float         yaw = 0.0f;     // degrees: swing the blades left/right around the handle bar
		float         tilt = 0.0f;    // degrees: tip the blades up (+) or down (-)
		std::uint32_t reloadKey = 68; // F10: re-read the ini without restarting
		bool  vanillaShockFX = true;                     // use vanilla's shock enchantment effect (lightning visuals)
	};
	inline Config cfg;

	static std::string Trim(std::string s)
	{
		const auto b = s.find_first_not_of(" \t\r\n");
		const auto e = s.find_last_not_of(" \t\r\n");
		return b == std::string::npos ? std::string{} : s.substr(b, e - b + 1);
	}

	static void LoadConfig()
	{
		std::ifstream file("Data/SKSE/Plugins/EnergySword.ini");
		if (!file) return;
		std::string line;
		while (std::getline(file, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '[') continue;
			const auto eq = line.find('=');
			if (eq == std::string::npos) continue;
			auto key = Trim(line.substr(0, eq));
			auto val = Trim(line.substr(eq + 1));
			if (const auto c = val.find(';'); c != std::string::npos) val = Trim(val.substr(0, c));
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			try {
				if (key == "aligngrip") cfg.alignGrip = std::stoi(val) != 0;
				else if (key == "gripx") cfg.gripX = std::stof(val);
				else if (key == "gripy") cfg.gripY = std::stof(val);
				else if (key == "gripz") cfg.gripZ = std::stof(val);
				else if (key == "twist") cfg.twist = std::stof(val);
				else if (key == "yaw") cfg.yaw = std::stof(val);
				else if (key == "style") cfg.style = std::stoi(val);
				else if (key == "reloadkey") cfg.reloadKey = static_cast<std::uint32_t>(std::stoul(val));
				else if (key == "tilt") cfg.tilt = std::stof(val);
				else if (key == "vanillashockfx") cfg.vanillaShockFX = std::stoi(val) != 0;
			} catch (...) {
				SKSE::log::warn("Bad value for {}: {}", key, val);
			}
		}
		SKSE::log::info("Config loaded");
	}

	// ------------------------------------------------------------------
	// Forms
	// ------------------------------------------------------------------
	inline RE::TESObjectWEAP*    sword = nullptr;
	inline RE::EnchantmentItem* enchant = nullptr;

	static void LookupForms()
	{
		auto dh = RE::TESDataHandler::GetSingleton();
		if (!dh) return;
		sword = dh->LookupForm<RE::TESObjectWEAP>(kWeaponID, kPluginFile);
		enchant = dh->LookupForm<RE::EnchantmentItem>(kEnchantID, kPluginFile);
		if (!sword || !enchant) {
			SKSE::log::error("EnergySword.esp not loaded: enable it in your load order");
			return;
		}

		// Sounds, impacts and keywords of a normal sword: the vanilla Iron Sword (Skyrim.esm 00012EB7)
		RE::TESObjectWEAP* tmpl = dh->LookupForm<RE::TESObjectWEAP>(0x12EB7, "Skyrim.esm"sv);
		if (!tmpl || !tmpl->IsOneHandedSword()) {
			tmpl = nullptr;
			for (auto w : dh->GetFormArray<RE::TESObjectWEAP>()) {
				if (w && w != sword && w->IsOneHandedSword() && !w->formEnchanting && w->impactDataSet && w->attackSound) {
					tmpl = w;
					break;
				}
			}
		}
		if (tmpl) {
			sword->impactDataSet = tmpl->impactDataSet;
			sword->attackSound = tmpl->attackSound;
			sword->attackSound2D = tmpl->attackSound2D;
			sword->attackFailSound = tmpl->attackFailSound;
			sword->idleSound = tmpl->idleSound;
			sword->equipSound = tmpl->equipSound;
			sword->unequipSound = tmpl->unequipSound;
			sword->pickupSound = tmpl->pickupSound;
			sword->putdownSound = tmpl->putdownSound;
			sword->blockBashImpactDataSet = tmpl->blockBashImpactDataSet;
			sword->altBlockMaterialType = tmpl->altBlockMaterialType;
			tmpl->ForEachKeyword([](RE::BGSKeyword* kw) {
				if (kw && !sword->HasKeyword(kw)) sword->AddKeyword(kw);
				return RE::BSContainer::ForEachResult::kContinue;
			});
			SKSE::log::info("Energy Sword uses sounds/impacts/keywords of {:08X} ({}), {} keywords", tmpl->GetFormID(), tmpl->GetName(), sword->GetNumKeywords());
		} else {
			SKSE::log::warn("No vanilla sword found to copy sounds from");
		}

		// Shock: vanilla's shock-damage weapon enchantment effect (Health damage resisted by shock resistance,
		// contact delivery) carries the lightning-on-the-blade visuals. Use it in our enchantment.
		if (cfg.vanillaShockFX) {
			RE::EffectSetting* shock = nullptr;
			for (auto e : dh->GetFormArray<RE::EnchantmentItem>()) {
				if (!e || e == enchant || e->GetDelivery() != RE::MagicSystem::Delivery::kTouch) continue;
				for (auto eff : e->effects) {
					auto m = eff ? eff->baseEffect : nullptr;
					if (m && m->data.resistVariable == RE::ActorValue::kResistShock && m->data.primaryAV == RE::ActorValue::kHealth &&
						(m->data.enchantShader || m->data.enchantEffectArt)) {
						shock = m;
						break;
					}
				}
				if (shock) {
					SKSE::log::info("Shock effect {:08X} from enchantment {:08X} ({})", shock->GetFormID(), e->GetFormID(), e->GetName());
					break;
				}
			}
			if (shock && !enchant->effects.empty() && enchant->effects[0]) {
				enchant->effects[0]->baseEffect = shock;
			} else {
				SKSE::log::warn("Vanilla shock enchantment not found: shock damage works, but without the lightning visuals");
			}
		}
		SKSE::log::info("Energy Sword ready: weapon {:08X}", sword->GetFormID());
	}

	// ------------------------------------------------------------------
	// Grip: put the handle bar in the fist, blades out the front of the knuckles
	// ------------------------------------------------------------------
	static RE::NiPoint3 Normalized(const RE::NiPoint3& v)
	{
		const float l = v.Length();
		return l > 1e-5f ? v / l : RE::NiPoint3{ 0, 0, 1 };
	}
	static RE::NiPoint3 Cross(const RE::NiPoint3& a, const RE::NiPoint3& b)
	{
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}
	static RE::NiPoint3 Rotate(const RE::NiPoint3& v, const RE::NiPoint3& axis, float rad)
	{
		const float c = std::cos(rad), s = std::sin(rad);
		return v * c + Cross(axis, v) * s + axis * (axis.Dot(v) * (1.0f - c));
	}

	static void AlignIn(RE::NiAVObject* root)
	{
		if (!root) return;
		auto node = root->GetObjectByName(RE::BSFixedString(kRootName));
		if (!node || !node->parent) return;
		// only while it's in the hand (the right-hand WEAPON node), not sheathed on the hip
		const char* parentName = node->parent->name.c_str();
		if (!parentName || std::string_view(parentName) != "WEAPON") return;

		auto bone = [&](const char* n) { return root->GetObjectByName(RE::BSFixedString(n)); };
		auto hand = bone("NPC R Hand [RHnd]");
		auto index = bone("NPC R Finger10 [RF10]");
		auto middle = bone("NPC R Finger20 [RF20]");
		auto pinky = bone("NPC R Finger40 [RF40]");
		if (!hand || !index || !middle || !pinky) return;
		auto indexTip = bone("NPC R Finger12 [RF12]");
		auto pinkyTip = bone("NPC R Finger42 [RF42]");

		const RE::NiPoint3 H = hand->world.translate, I = index->world.translate, M = middle->world.translate, P = pinky->world.translate;
		const RE::NiPoint3 fist = Normalized(I - P);              // along the knuckles, pinky -> index
		RE::NiPoint3       knuckles = M - H;
		knuckles = Normalized(knuckles - fist * knuckles.Dot(fist));  // the way the knuckles face
		const RE::NiPoint3 across = Normalized(Cross(knuckles, fist));
		RE::NiPoint3 center = (I + P) * 0.5f;                     // inside the curled fingers
		if (indexTip && pinkyTip) {
			center = (I + P + indexTip->world.translate + pinkyTip->world.translate) * 0.25f;
		}
		// blade direction B and handle-bar direction U
		RE::NiPoint3 B = cfg.style == 0 ? knuckles : fist;
		RE::NiPoint3 U = cfg.style == 0 ? fist : across;
		constexpr float d2r = 0.0174533f;
		if (cfg.twist != 0.0f) U = Rotate(U, B, cfg.twist * d2r);
		if (cfg.yaw != 0.0f) B = Rotate(B, U, cfg.yaw * d2r);
		if (cfg.tilt != 0.0f) {
			const RE::NiPoint3 side = Normalized(Cross(B, U));
			B = Rotate(B, side, cfg.tilt * d2r);
			U = Rotate(U, side, cfg.tilt * d2r);
		}
		B = Normalized(B);
		U = Normalized(U - B * U.Dot(B));
		const RE::NiPoint3 x = Normalized(Cross(B, U));
		const RE::NiPoint3 k = B, g = U;
		// mesh axes: X across, Y along the blades, Z up the handle bar, origin at the grip
		RE::NiMatrix3 R;
		for (int r = 0; r < 3; ++r) {
			R.entry[r][0] = (&x.x)[r];
			R.entry[r][1] = (&k.x)[r];
			R.entry[r][2] = (&g.x)[r];
		}
		const RE::NiPoint3 pos = center + x * cfg.gripX + k * cfg.gripY + g * cfg.gripZ;

		const auto& pw = node->parent->world;
		RE::NiTransform desired;
		desired.rotate = R;
		desired.translate = pos;
		desired.scale = node->world.scale;
		RE::NiTransform local = pw.Invert() * desired;
		local.scale = node->local.scale;
		node->local = local;
		RE::NiUpdateData ud;
		node->Update(ud);
	}

	static void OnFrame()
	{
		if (!cfg.alignGrip || !sword) return;
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->AsActorState()->IsWeaponDrawn()) return;
		auto right = player->GetEquippedObject(false);
		if (right != sword) return;
		AlignIn(player->Get3D(true));   // first person
		AlignIn(player->Get3D(false));  // third person
	}

	static void Notify(const char* msg)
	{
		using func_t = void (*)(const char*, const char*, bool);
		static REL::Relocation<func_t> func{ REL::RelocationID(52050, 52933) };
		func(msg, nullptr, true);
	}

	// F10 (by default): re-read EnergySword.ini so the grip can be tuned while the game runs
	class InputHandler : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputHandler* Get()
		{
			static InputHandler instance;
			return &instance;
		}
		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			for (auto e = a_event ? *a_event : nullptr; e; e = e->next) {
				auto button = e->AsButtonEvent();
				if (!button || !button->IsDown() || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;
				if (button->GetIDCode() == cfg.reloadKey) {
					LoadConfig();
					Notify(std::format("Energy Sword ini reloaded (style {}, twist {}, yaw {}, tilt {})", cfg.style, cfg.twist, cfg.yaw, cfg.tilt).c_str());
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	struct PlayerUpdateHook
	{
		static void thunk(RE::PlayerCharacter* a_this, float a_delta)
		{
			func(a_this, a_delta);
			OnFrame();
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
			func = vtbl.write_vfunc(0xAD, thunk);
			SKSE::log::info("Player update hook installed");
		}
	};
}

static void SetupLog()
{
	auto dir = SKSE::log::log_directory();
	if (!dir) return;
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((*dir / "EnergySword.log").string(), true);
	auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);
	spdlog::set_default_logger(std::move(log));
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	SetupLog();
	SKSE::log::info("Energy Sword loading");
	if (REL::Module::IsVR()) {
		SKSE::log::error("Skyrim VR is not supported");
		return false;
	}
	ES::LoadConfig();
	ES::PlayerUpdateHook::Install();
	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
		if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
			ES::LookupForms();
			RE::BSInputDeviceManager::GetSingleton()->AddEventSink(ES::InputHandler::Get());
		}
	});
	return true;
}
