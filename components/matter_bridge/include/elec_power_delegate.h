#pragma once

// ============================================================================
// Delegate pour le cluster ElectricalPowerMeasurement (0x0090)
// Stocke les valeurs recues du ZLinky et les retourne au framework Matter
//
// Le serveur CHIP de ce cluster est un AttributeAccessInterface : il LIT ces
// valeurs a la demande, mais ne signale jamais leur changement. Un controleur
// abonne (Home Assistant) ne recoit donc une nouvelle valeur que si le delegate
// appelle lui-meme MatterReportingAttributeChangeCallback() ; sans cela il
// garde indefiniment la valeur lue a l'abonnement.
// ============================================================================

#include <app/clusters/electrical-power-measurement-server/electrical-power-measurement-server.h>
#include <app/reporting/reporting.h>
#include <esp_matter_core.h>
#include "freertos/FreeRTOS.h"
#include "matter_bridge.h"

namespace lixee_epm_attr = chip::app::Clusters::ElectricalPowerMeasurement::Attributes;

class ElecPowerDelegate : public chip::app::Clusters::ElectricalPowerMeasurement::Delegate
{
public:
    ElecPowerDelegate() = default;

    // --- Setters (appeles depuis la tache de reception ZiGate) ---

    // Puissance active MESUREE : prend definitivement le pas sur
    // l'approximation par la puissance apparente (voir SetApparentPower).
    void SetActivePower(int64_t mw)
    {
        m_active_is_measured = true;
        Update(m_active_power, mw, lixee_epm_attr::ActivePower::Id);
    }

    // Les mesures RMS du ZLinky sont publiees sur Voltage et ActiveCurrent,
    // les attributs qu'affiche Home Assistant (crees sur l'endpoint par
    // matter_bridge.cpp).
    void SetRMSVoltage(int64_t mv) { Update(m_rms_voltage, mv, lixee_epm_attr::Voltage::Id); }
    void SetRMSCurrent(int64_t ma) { Update(m_rms_current, ma, lixee_epm_attr::ActiveCurrent::Id); }

    // Puissance apparente TOTALE (ZLinky 0x0B04/0x0306, SINSTS). Prioritaire :
    // en triphase standard, 0x050F ne contient que la phase 1.
    void SetTotalApparentPower(int64_t mva)
    {
        m_has_total_apparent = true;
        PublishApparent(mva);
    }

    // Puissance apparente de l'attribut 0x050F : PAPP (historique, total
    // y compris en triphase), SINSTS en standard monophase, mais SINSTS1
    // (phase 1 seule) en standard triphase. Ignoree des qu'un total existe.
    void SetApparentPower(int64_t mva)
    {
        if (m_has_total_apparent) return;
        PublishApparent(mva);
    }

private:
    void PublishApparent(int64_t mva)
    {
        Update(m_apparent_power, mva, lixee_epm_attr::ApparentPower::Id);
        // Le ZLinky ne publie aucune puissance active instantanee : le Linky
        // ne la transmet pas en TIC. ActivePower est le seul attribut de mesure
        // OBLIGATOIRE du cluster, et c'est celui qu'affiche Home Assistant :
        // faute de mesure active, on l'aligne sur la puissance apparente (cos
        // phi suppose egal a 1), jusqu'a reception d'une vraie puissance active.
        if (!m_active_is_measured) {
            Update(m_active_power, mva, lixee_epm_attr::ActivePower::Id);
        }
    }

public:
    // --- Mandatory getters ---
    chip::app::Clusters::ElectricalPowerMeasurement::PowerModeEnum GetPowerMode() override
    {
        return chip::app::Clusters::ElectricalPowerMeasurement::PowerModeEnum::kAc;
    }

    uint8_t GetNumberOfMeasurementTypes() override { return 1; }

    // --- Accuracy list (mandatory, return 1 entry for ActivePower) ---
    CHIP_ERROR StartAccuracyRead() override { m_accuracy_index = 0; return CHIP_NO_ERROR; }
    CHIP_ERROR GetAccuracyByIndex(uint8_t index,
        chip::app::Clusters::ElectricalPowerMeasurement::Structs::MeasurementAccuracyStruct::Type &entry) override
    {
        if (index > 0) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        entry.measurementType = chip::app::Clusters::ElectricalPowerMeasurement::MeasurementTypeEnum::kActivePower;
        entry.measured = true;
        entry.minMeasuredValue = 0;
        entry.maxMeasuredValue = 100000000;  // 100 kW en mW
        // accuracyRanges est requis mais on peut le laisser vide
        entry.accuracyRanges = chip::app::DataModel::List<
            chip::app::Clusters::ElectricalPowerMeasurement::Structs::MeasurementAccuracyRangeStruct::Type>();
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR EndAccuracyRead() override { return CHIP_NO_ERROR; }

    // --- Ranges list (optional, empty) ---
    CHIP_ERROR StartRangesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetRangeByIndex(uint8_t,
        chip::app::Clusters::ElectricalPowerMeasurement::Structs::MeasurementRangeStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndRangesRead() override { return CHIP_NO_ERROR; }

    // --- Harmonic lists (optional, empty) ---
    CHIP_ERROR StartHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetHarmonicCurrentsByIndex(uint8_t,
        chip::app::Clusters::ElectricalPowerMeasurement::Structs::HarmonicMeasurementStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR StartHarmonicPhasesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetHarmonicPhasesByIndex(uint8_t,
        chip::app::Clusters::ElectricalPowerMeasurement::Structs::HarmonicMeasurementStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndHarmonicPhasesRead() override { return CHIP_NO_ERROR; }

    // --- Scalar measurement getters ---
    // Appeles par le serveur depuis la tache CHIP, verrou de la pile tenu.
    // Tout autre appelant doit prendre ce verrou (voir matter_bridge_get_energy_data).
    chip::app::DataModel::Nullable<int64_t> GetVoltage()         override { return m_rms_voltage; }
    chip::app::DataModel::Nullable<int64_t> GetActiveCurrent()   override { return m_rms_current; }
    chip::app::DataModel::Nullable<int64_t> GetReactiveCurrent() override { return chip::app::DataModel::Nullable<int64_t>(); }
    chip::app::DataModel::Nullable<int64_t> GetApparentCurrent() override { return chip::app::DataModel::Nullable<int64_t>(); }
    chip::app::DataModel::Nullable<int64_t> GetActivePower()     override { return m_active_power; }
    chip::app::DataModel::Nullable<int64_t> GetReactivePower()   override { return chip::app::DataModel::Nullable<int64_t>(); }
    chip::app::DataModel::Nullable<int64_t> GetApparentPower()   override { return m_apparent_power; }
    chip::app::DataModel::Nullable<int64_t> GetRMSVoltage()      override { return m_rms_voltage; }
    chip::app::DataModel::Nullable<int64_t> GetRMSCurrent()      override { return m_rms_current; }
    chip::app::DataModel::Nullable<int64_t> GetRMSPower()        override { return m_active_power; }
    chip::app::DataModel::Nullable<int64_t> GetFrequency()       override { return chip::app::DataModel::Nullable<int64_t>(); }
    chip::app::DataModel::Nullable<int64_t> GetPowerFactor()     override { return chip::app::DataModel::Nullable<int64_t>(); }
    chip::app::DataModel::Nullable<int64_t> GetNeutralCurrent()  override { return chip::app::DataModel::Nullable<int64_t>(); }

private:
    /**
     * Met a jour un champ et, s'il change, le signale aux abonnes Matter.
     *
     * Ecriture et signalement se font sous le verrou de la pile CHIP :
     *  - le serveur lit ces champs depuis la tache CHIP, et un int64 n'est pas
     *    ecrit de facon atomique sur ESP32 (lecture dechiree possible) ;
     *  - MatterReportingAttributeChangeCallback() exige ce verrou (assert).
     */
    void Update(chip::app::DataModel::Nullable<int64_t> &field, int64_t value, chip::AttributeId attr)
    {
        // Avant start(), le verrou de la pile n'existe pas encore et personne
        // ne lit ces champs : simple affectation.
        if (!matter_bridge_is_started()) {
            field.SetNonNull(value);
            return;
        }

        esp_matter::lock::status_t st = esp_matter::lock::chip_stack_lock(portMAX_DELAY);
        if (st == esp_matter::lock::FAILED) {
            return;
        }

        bool changed = field.IsNull() || field.Value() != value;
        field.SetNonNull(value);

        // mEndpointId est renseigne par l'Instance du cluster a son
        // initialisation ; 0 (endpoint racine) signifie "pas encore initialise".
        if (changed && mEndpointId != 0 && mEndpointId != chip::kInvalidEndpointId) {
            MatterReportingAttributeChangeCallback(mEndpointId,
                chip::app::Clusters::ElectricalPowerMeasurement::Id, attr);
        }

        if (st == esp_matter::lock::SUCCESS) {
            esp_matter::lock::chip_stack_unlock();
        }
    }

    chip::app::DataModel::Nullable<int64_t> m_active_power;
    chip::app::DataModel::Nullable<int64_t> m_rms_voltage;
    chip::app::DataModel::Nullable<int64_t> m_rms_current;
    chip::app::DataModel::Nullable<int64_t> m_apparent_power;
    bool    m_active_is_measured = false;
    bool    m_has_total_apparent = false;
    uint8_t m_accuracy_index = 0;
};
