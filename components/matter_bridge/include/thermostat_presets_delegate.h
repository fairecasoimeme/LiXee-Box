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

        bool active_changed = !m_synced || m_frost_active != frost_active;
        bool presets_changed = m_comfort_sp != comfort || m_frost_sp != frost;
        m_comfort_sp = comfort;
        m_frost_sp = frost;
        m_frost_active = frost_active;
        m_synced = true;
        if (!active_changed && !presets_changed) return;

        esp_matter::lock::status_t st = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
        if (st == esp_matter::lock::FAILED) return;
        if (presets_changed) {
            MatterReportingAttributeChangeCallback(m_endpoint_id, chip::app::Clusters::Thermostat::Id,
                chip::app::Clusters::Thermostat::Attributes::Presets::Id);
        }
        if (active_changed) {
            MatterReportingAttributeChangeCallback(m_endpoint_id, chip::app::Clusters::Thermostat::Id,
                chip::app::Clusters::Thermostat::Attributes::ActivePresetHandle::Id);
        }
        if (st == esp_matter::lock::SUCCESS) {
            esp_matter::lock::chip_stack_unlock();
        }
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
