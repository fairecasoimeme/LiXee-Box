#pragma once

// ============================================================================
// Delegates pour l'endpoint Water Heater (0x050F) :
//   - WaterHeaterManagementDelegate (cluster 0x0094) : Boost / CancelBoost,
//     HeaterTypes / HeatDemand / BoostState.
//   - WaterHeaterModeDelegate (cluster 0x009E, derive de Mode Base) : 3 modes
//     fixes Off / Manual / Timed.
//
// Comme ElecPowerDelegate (voir elec_power_delegate.h) : le serveur CHIP lit
// ces valeurs a la demande (AttributeAccessInterface), donc tout changement
// cote regulation doit etre signale explicitement via
// MatterReportingAttributeChangeCallback() pour qu'un controleur abonne le
// voie. Les commandes (Boost/CancelBoost/ChangeToMode) arrivent dans la tache
// CHIP : comme pour les ecritures d'attribut du thermostat virtuel
// (thermostat.cpp, matter_write_cb/MSG_WRITE), elles sont transmises au
// module proprietaire via matter_bridge_dispatch_virtual_command() plutot que
// traitees ici (la tache CHIP ne doit jamais prendre le verrou des zones du
// module water_heater).
//
// CurrentMode (WaterHeaterMode::Attributes::CurrentMode, SupportedModes[].mode)
// est un uint8_t SANS signification imposee par la specification : ce sont de
// simples index choisis par l'implementation (ici 0/1/2). Le sens expose au
// controleur passe par les ModeTag (uint16_t, voir Mode_WaterHeater.xml) :
// 0x4000 Off / 0x4001 Manual / 0x4002 Timed -- ne pas confondre les deux
// espaces de valeurs (l'erreur serait silencieuse : uint8_t tronquerait
// 0x4000 a 0x00 sans avertissement).
// ============================================================================

#include <app/clusters/water-heater-management-server/water-heater-management-server.h>
#include <app/clusters/mode-base-server/mode-base-server.h>
#include <app/reporting/reporting.h>
#include <esp_matter_core.h>
#include "matter_bridge.h"

namespace lixee_whm = chip::app::Clusters::WaterHeaterManagement;
namespace lixee_mode = chip::app::Clusters::ModeBase;

// Valeurs CurrentMode (uint8_t, arbitraires) exposees par water_heater.cpp
#define WH_MODE_OFF     0u
#define WH_MODE_MANUAL  1u
#define WH_MODE_TIMED   2u

class WaterHeaterManagementDelegate : public lixee_whm::Delegate
{
public:
    WaterHeaterManagementDelegate() = default;

    // --- Setters (appeles depuis la tache du module water_heater) ---

    // Reflete l'etat reel du contacteur pilotant la resistance.
    void SetHeatDemand(bool on)
    {
        bool was = m_heat_demand.Raw() != 0;
        if (on == was) return;
        auto v = on ? lixee_whm::WaterHeaterHeatSourceBitmap::kImmersionElement1
                    : static_cast<lixee_whm::WaterHeaterHeatSourceBitmap>(0);
        Report([this, v] { m_heat_demand = chip::BitMask<lixee_whm::WaterHeaterHeatSourceBitmap>(v); },
               lixee_whm::Attributes::HeatDemand::Id);
    }

    // true/false : evenement BoostEnded genere au retour a l'inactif (BoostStarted
    // est genere directement par HandleBoost, qui connait les parametres du client).
    void SetBoostActive(bool active)
    {
        bool was = m_boost_state == lixee_whm::BoostStateEnum::kActive;
        if (active == was) return;
        Report([this, active] {
            m_boost_state = active ? lixee_whm::BoostStateEnum::kActive : lixee_whm::BoostStateEnum::kInactive;
        }, lixee_whm::Attributes::BoostState::Id);
        if (!active) GenerateBoostEndedEvent();
    }

    // --- Getters obligatoires du Delegate ---
    chip::BitMask<lixee_whm::WaterHeaterHeatSourceBitmap> GetHeaterTypes() override
    {
        return chip::BitMask<lixee_whm::WaterHeaterHeatSourceBitmap>(
            lixee_whm::WaterHeaterHeatSourceBitmap::kImmersionElement1);
    }
    chip::BitMask<lixee_whm::WaterHeaterHeatSourceBitmap> GetHeatDemand() override { return m_heat_demand; }
    uint16_t GetTankVolume() override { return 0; }                    // feature EM non supportee
    chip::Energy_mWh GetEstimatedHeatRequired() override { return 0; } // feature EM non supportee
    chip::Percent GetTankPercentage() override { return 0; }           // feature TP non supportee
    lixee_whm::BoostStateEnum GetBoostState() override { return m_boost_state; }

    // --- Commandes : transmises au module water_heater, reponse optimiste ---
    chip::Protocols::InteractionModel::Status HandleBoost(
        uint32_t duration, chip::Optional<bool> oneShot, chip::Optional<bool> emergencyBoost,
        chip::Optional<int16_t> temporarySetpoint, chip::Optional<chip::Percent>,
        chip::Optional<chip::Percent>) override
    {
        matter_wh_boost_t b = {};
        b.duration_s = duration;
        b.one_shot = oneShot.ValueOr(false);
        b.emergency = emergencyBoost.ValueOr(false);
        b.has_temp = temporarySetpoint.HasValue();
        b.temp_centidegrees = temporarySetpoint.ValueOr(0);
        matter_bridge_dispatch_virtual_command(mEndpointId, MATTER_VCMD_WH_BOOST, 0, &b);
        GenerateBoostStartedEvent(duration, oneShot, emergencyBoost, temporarySetpoint,
                                  chip::Optional<chip::Percent>(), chip::Optional<chip::Percent>());
        return chip::Protocols::InteractionModel::Status::Success;
    }

    chip::Protocols::InteractionModel::Status HandleCancelBoost() override
    {
        matter_bridge_dispatch_virtual_command(mEndpointId, MATTER_VCMD_WH_CANCEL_BOOST, 0, nullptr);
        return chip::Protocols::InteractionModel::Status::Success;
    }

private:
    // Meme schema que ElecPowerDelegate::Update() (elec_power_delegate.h) :
    // mutation + signalement sous le verrou de la pile CHIP.
    template <typename F>
    void Report(F mutate, chip::AttributeId attr)
    {
        if (!matter_bridge_is_started()) { mutate(); return; }
        esp_matter::lock::status_t st = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
        mutate();
        if (mEndpointId != 0 && mEndpointId != chip::kInvalidEndpointId) {
            MatterReportingAttributeChangeCallback(mEndpointId, lixee_whm::Id, attr);
        }
        if (st == esp_matter::lock::SUCCESS) esp_matter::lock::chip_stack_unlock();
    }

    chip::BitMask<lixee_whm::WaterHeaterHeatSourceBitmap> m_heat_demand{};
    lixee_whm::BoostStateEnum m_boost_state = lixee_whm::BoostStateEnum::kInactive;
};

class WaterHeaterModeDelegate : public lixee_mode::Delegate
{
public:
    WaterHeaterModeDelegate() = default;

    CHIP_ERROR Init() override { return CHIP_NO_ERROR; }

    CHIP_ERROR GetModeLabelByIndex(uint8_t modeIndex, chip::MutableCharSpan &label) override
    {
        if (modeIndex >= kModeCount) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        return chip::CopyCharSpanToMutableCharSpan(chip::CharSpan::fromCharString(kLabels[modeIndex]), label);
    }

    CHIP_ERROR GetModeValueByIndex(uint8_t modeIndex, uint8_t &value) override
    {
        if (modeIndex >= kModeCount) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        value = kValues[modeIndex];
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetModeTagsByIndex(uint8_t modeIndex,
        chip::app::DataModel::List<chip::app::Clusters::detail::Structs::ModeTagStruct::Type> &tags) override
    {
        if (modeIndex >= kModeCount) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        if (tags.size() < 1) return CHIP_ERROR_BUFFER_TOO_SMALL;
        tags[0].value = kTags[modeIndex];
        tags.reduce_size(1);
        return CHIP_NO_ERROR;
    }

    // Toujours accepte : les 3 modes sont valides en permanence, la decision
    // (regulation suspendue hors plage horaire en Timed, etc.) se prend dans
    // le module water_heater, pas ici.
    void HandleChangeToMode(uint8_t newMode, lixee_mode::Commands::ChangeToModeResponse::Type &response) override
    {
        bool known = (newMode == WH_MODE_OFF || newMode == WH_MODE_MANUAL || newMode == WH_MODE_TIMED);
        response.status = static_cast<uint8_t>(known
            ? lixee_mode::StatusCode::kSuccess
            : lixee_mode::StatusCode::kUnsupportedMode);
        // ModeBase::Delegate n'a pas de mEndpointId (contrairement a
        // WaterHeaterManagement::Delegate) : seul mInstance->GetEndpointId() le donne.
        if (known && mInstance) {
            matter_bridge_dispatch_virtual_command(mInstance->GetEndpointId(), MATTER_VCMD_WH_CHANGE_MODE,
                                                   newMode, nullptr);
        }
    }

private:
    static constexpr uint8_t kModeCount = 3;
    static constexpr uint8_t kValues[kModeCount] = { WH_MODE_OFF, WH_MODE_MANUAL, WH_MODE_TIMED };
    // ModeTag (uint16_t) : espace de valeurs distinct de CurrentMode, voir Mode_WaterHeater.xml
    static constexpr uint16_t kTags[kModeCount] = { 0x4000, 0x4001, 0x4002 };
    static constexpr const char *kLabels[kModeCount] = { "Arret", "Marche forcee", "Programme" };
};
