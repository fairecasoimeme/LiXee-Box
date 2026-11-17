#pragma once

// ============================================================================
// Delegate pour la feature Presets du cluster Thermostat (0x0201)
//
// Expose 2 presets fixes et non editables par zone de thermostat virtuel :
// "Confort" (handle 0x01, scenario Occupied) et "Hors-gel" (handle 0x02,
// scenario Vacation). Leurs consignes (Presets.HeatingSetpoint) et l'etat
// actif (ActivePresetHandle) sont pousses depuis la zone (voir
// matter_bridge_set_thermostat_presets(), appele par thermostat.cpp) : comme
// pour ElecPowerDelegate, le serveur CHIP ne fait que LIRE ces valeurs a la
// demande, c'est au delegate de signaler leurs changements
// (MatterReportingAttributeChangeCallback), sans quoi un controleur abonne
// (Home Assistant) ne les verrait jamais evoluer.
//
// Pas de support d'edition des presets depuis un controleur (AppendToPending
// PresetList etc. renvoient une erreur) : scope volontairement reduit au
// choix parmi les 2 presets fixes (issue #51).
// ============================================================================

#include <app/clusters/thermostat-server/thermostat-delegate.h>
#include <app/clusters/thermostat-server/thermostat-server.h>
#include <app/reporting/reporting.h>
#include <esp_matter_core.h>
#include <lib/support/CodeUtils.h>
#include "elec_power_delegate.h"   // lixee_chip_lock()/lixee_chip_unlock() : esp-matter 1.5 ne publie plus lock::chip_stack_lock()

class ThermostatPresetsDelegate : public chip::app::Clusters::Thermostat::Delegate
{
public:
    explicit ThermostatPresetsDelegate(int endpoint_id) : m_endpoint_id(endpoint_id) {}

    /**
     * Pousse l'etat courant de la zone (appele depuis la tache du thermostat
     * virtuel, voir matter_bridge_set_thermostat_presets()). Consignes en
     * centiemes de degre. Signale les changements aux controleurs abonnes.
     */
    void SyncFromZone(int32_t comfort_setpoint, int32_t frost_setpoint, bool frost_active)
    {
        auto clamp16 = [](int32_t v) -> int16_t {
            return (int16_t)(v < -27315 ? -27315 : (v > 32767 ? 32767 : v));
        };
        int16_t comfort = clamp16(comfort_setpoint);
        int16_t frost = clamp16(frost_setpoint);

        // Verrou pris avant de toucher les champs partages (lus par le
        // thread CHIP via GetPresetAtIndex()/GetActivePresetHandle()), pas
        // apres : sinon le thread CHIP peut lire un etat partiellement mis
        // a jour pendant la fenetre non protegee.
        bool held = lixee_chip_lock();
        bool active_changed = !m_synced || m_frost_active != frost_active;
        bool presets_changed = m_comfort_sp != comfort || m_frost_sp != frost;
        m_comfort_sp = comfort;
        m_frost_sp = frost;
        m_frost_active = frost_active;
        m_synced = true;
        if (presets_changed) {
            MatterReportingAttributeChangeCallback(m_endpoint_id, chip::app::Clusters::Thermostat::Id,
                chip::app::Clusters::Thermostat::Attributes::Presets::Id);
        }
        if (active_changed) {
            MatterReportingAttributeChangeCallback(m_endpoint_id, chip::app::Clusters::Thermostat::Id,
                chip::app::Clusters::Thermostat::Attributes::ActivePresetHandle::Id);
        }
        lixee_chip_unlock(held);
    }

    // --- chip::app::Clusters::Thermostat::Delegate ---

    std::optional<chip::System::Clock::Milliseconds16> GetMaxAtomicWriteTimeout(chip::AttributeId) override
    {
        return std::nullopt;   // pas d'ecriture atomique de Presets supportee (liste fixe)
    }

    uint8_t GetNumberOfPresets() override { return kPresetCount; }

    CHIP_ERROR GetPresetTypeAtIndex(size_t index,
        chip::app::Clusters::Thermostat::Structs::PresetTypeStruct::Type &presetType) override
    {
        if (index >= kPresetCount) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        using chip::app::Clusters::Thermostat::PresetScenarioEnum;
        using chip::app::Clusters::Thermostat::PresetTypeFeaturesBitmap;
        presetType.presetScenario = (index == 1) ? PresetScenarioEnum::kVacation : PresetScenarioEnum::kOccupied;
        presetType.numberOfPresets = 1;
        // SupportsNames : sans ca, certains controleurs (Home Assistant) n'exposent
        // pas le preset faute de nom a afficher.
        presetType.presetTypeFeatures = chip::BitMask<PresetTypeFeaturesBitmap>(PresetTypeFeaturesBitmap::kSupportsNames);
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetPresetAtIndex(size_t index,
        chip::app::Clusters::Thermostat::PresetStructWithOwnedMembers &preset) override
    {
        if (index >= kPresetCount) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        using namespace chip;
        using chip::app::Clusters::Thermostat::PresetScenarioEnum;
        bool frost = (index == 1);
        uint8_t handle = frost ? kHandleFrost : kHandleComfort;
        preset.SetPresetScenario(frost ? PresetScenarioEnum::kVacation : PresetScenarioEnum::kOccupied);
        ReturnErrorOnFailure(preset.SetPresetHandle(app::DataModel::MakeNullable(ByteSpan(&handle, 1))));
        CharSpan name = frost ? CharSpan::fromCharString("Hors-gel") : CharSpan::fromCharString("Confort");
        ReturnErrorOnFailure(preset.SetName(MakeOptional(app::DataModel::MakeNullable(name))));
        preset.SetCoolingSetpoint(NullOptional);
        preset.SetHeatingSetpoint(MakeOptional(frost ? m_frost_sp : m_comfort_sp));
        preset.SetBuiltIn(app::DataModel::MakeNullable(true));
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetActivePresetHandle(
        chip::app::DataModel::Nullable<chip::MutableByteSpan> &activePresetHandle) override
    {
        if (!m_synced) {
            activePresetHandle.SetNull();
            return CHIP_NO_ERROR;
        }
        uint8_t handle = m_frost_active ? kHandleFrost : kHandleComfort;
        ReturnErrorOnFailure(chip::CopySpanToMutableSpan(chip::ByteSpan(&handle, 1), activePresetHandle.Value()));
        activePresetHandle.Value().reduce_size(1);
        return CHIP_NO_ERROR;
    }

    // Appele quand un controleur invoque SetActivePresetRequest : repercute le
    // choix sur la zone (voir matter_bridge.cpp, qui a acces au callback
    // enregistre par thermostat.cpp). Definie hors ligne (matter_bridge.cpp).
    CHIP_ERROR SetActivePresetHandle(
        const chip::app::DataModel::Nullable<chip::ByteSpan> &newActivePresetHandle) override;

    // --- Edition des presets depuis un controleur : non supportee en v1 ---
    void InitializePendingPresets() override {}
    CHIP_ERROR AppendToPendingPresetList(const chip::app::Clusters::Thermostat::PresetStructWithOwnedMembers &) override
    {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR GetPendingPresetAtIndex(size_t, chip::app::Clusters::Thermostat::PresetStructWithOwnedMembers &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR CommitPendingPresets() override { return CHIP_NO_ERROR; }
    void ClearPendingPresetList() override {}

    // --- Suggestions (cluster Thermostat, esp_matter 1.5.1) et Schedules :
    //     features non activees sur nos thermostats virtuels (ni ScheduleConfiguration
    //     ni MatterScheduleConfiguration/ThermostatSuggestions dans add_profile_clusters()),
    //     mais la Delegate de base les declare pures virtuelles : a implementer quel que
    //     soit l'etat des feature flags pour pouvoir instancier la classe.
    uint8_t GetMaxThermostatSuggestions() override { return 0; }
    uint8_t GetNumberOfThermostatSuggestions() override { return 0; }
    CHIP_ERROR GetThermostatSuggestionAtIndex(size_t,
        chip::app::Clusters::Thermostat::ThermostatSuggestionStructWithOwnedMembers &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    void GetCurrentThermostatSuggestion(
        chip::app::DataModel::Nullable<chip::app::Clusters::Thermostat::ThermostatSuggestionStructWithOwnedMembers> &currentThermostatSuggestion) override
    {
        currentThermostatSuggestion.SetNull();
    }
    chip::app::DataModel::Nullable<chip::app::Clusters::Thermostat::ThermostatSuggestionNotFollowingReasonBitmap>
    GetThermostatSuggestionNotFollowingReason() override
    {
        return chip::app::DataModel::Nullable<chip::app::Clusters::Thermostat::ThermostatSuggestionNotFollowingReasonBitmap>();
    }
    CHIP_ERROR AppendToThermostatSuggestionsList(
        const chip::app::Clusters::Thermostat::Structs::ThermostatSuggestionStruct::Type &) override
    {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
    CHIP_ERROR RemoveFromThermostatSuggestionsList(size_t) override { return CHIP_ERROR_NOT_FOUND; }
    CHIP_ERROR GetUniqueID(uint8_t &) override { return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED; }
    CHIP_ERROR ReEvaluateCurrentSuggestion() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetScheduleTypeAtIndex(size_t,
        chip::app::Clusters::Thermostat::Structs::ScheduleTypeStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }

    int EndpointId() const { return m_endpoint_id; }

private:
    static constexpr uint8_t kPresetCount = 2;
    static constexpr uint8_t kHandleComfort = 0x01;
    static constexpr uint8_t kHandleFrost = 0x02;

    int     m_endpoint_id;
    int16_t m_comfort_sp = 0;
    int16_t m_frost_sp = 0;
    bool    m_frost_active = false;
    bool    m_synced = false;        // aucun SyncFromZone() recu depuis la creation
};
