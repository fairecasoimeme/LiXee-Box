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
        // Premiere mesure : ActivePower passe de "estimee" a "mesuree" dans
        // la liste Accuracy, a signaler aussi.
        bool first = !m_active_is_measured;
        m_active_is_measured = true;
        Update(m_active_power, mw, lixee_epm_attr::ActivePower::Id, first);
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

    // Types de mesure pris en charge : ceux dont l'attribut existe sur
    // l'endpoint (voir matter_bridge.cpp), meme si leur valeur est encore nulle.
    uint8_t GetNumberOfMeasurementTypes() override { return kMeasurementTypeCount; }

    // --- Accuracy list (obligatoire) : une entree par type de mesure, ---
    // --- ActivePower compris (exige par la specification)            ---
    CHIP_ERROR StartAccuracyRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetAccuracyByIndex(uint8_t index,
        chip::app::Clusters::ElectricalPowerMeasurement::Structs::MeasurementAccuracyStruct::Type &entry) override
    {
        using chip::app::Clusters::ElectricalPowerMeasurement::MeasurementTypeEnum;
        using Range = chip::app::Clusters::ElectricalPowerMeasurement::Structs::MeasurementAccuracyRangeStruct::Type;

        // Plages et precision (resolution d'une unite entiere : V, A, W, VA,
        // celle de la TIC Linky et des prises Zigbee). La specification veut
        // au moins une plage par type, avec FixedMax ou PercentMax.
        static Range s_ranges[kMeasurementTypeCount];
        static const struct { MeasurementTypeEnum type; int64_t max; uint64_t fixed; } k_types[kMeasurementTypeCount] = {
            { MeasurementTypeEnum::kVoltage,       400000,    1000 },  // 400 V en mV
            { MeasurementTypeEnum::kActiveCurrent, 100000,    1000 },  // 100 A en mA
            { MeasurementTypeEnum::kActivePower,   100000000, 1000 },  // 100 kW en mW
            { MeasurementTypeEnum::kApparentPower, 100000000, 1000 },  // 100 kVA en mVA
        };
        if (index >= kMeasurementTypeCount) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;

        Range &r = s_ranges[index];
        r.rangeMin = 0;
        r.rangeMax = k_types[index].max;
        r.fixedMax.SetValue(k_types[index].fixed);

        entry.measurementType  = k_types[index].type;
        // ActivePower n'est qu'estimee (puissance apparente, cos phi = 1)
        // tant qu'aucune vraie puissance active n'a ete recue.
        entry.measured         = k_types[index].type != MeasurementTypeEnum::kActivePower || m_active_is_measured;
        entry.minMeasuredValue = 0;
        entry.maxMeasuredValue = k_types[index].max;
        entry.accuracyRanges   = chip::app::DataModel::List<const Range>(&r, 1);
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
     * accuracy_changed signale aussi la liste Accuracy.
     */
    void Update(chip::app::DataModel::Nullable<int64_t> &field, int64_t value, chip::AttributeId attr,
                bool accuracy_changed = false)
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
        if (mEndpointId != 0 && mEndpointId != chip::kInvalidEndpointId) {
            if (changed) {
                MatterReportingAttributeChangeCallback(mEndpointId,
                    chip::app::Clusters::ElectricalPowerMeasurement::Id, attr);
            }
            if (accuracy_changed) {
                MatterReportingAttributeChangeCallback(mEndpointId,
                    chip::app::Clusters::ElectricalPowerMeasurement::Id, lixee_epm_attr::Accuracy::Id);
            }
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

    // Voltage, ActiveCurrent, ActivePower, ApparentPower
    static constexpr uint8_t kMeasurementTypeCount = 4;
};
