#pragma once

// ============================================================================
// Delegate pour la feature Presets du cluster Thermostat (0x0201)
//
// Expose 2 presets fixes "Confort" (handle 0x01, scenario Occupied) et
// "Hors-gel" (handle 0x02, scenario Vacation) par zone de thermostat
// virtuel, plus jusqu'a MATTER_PRESET_MAX_CUSTOM presets personnalises
// (handles 0x03+, issue #80) editables par un controleur Matter (flux
// AtomicRequest : InitializePendingPresets/AppendToPendingPresetList/
// CommitPendingPresets).
//
// Leurs consignes (Presets.HeatingSetpoint) et l'etat actif
// (ActivePresetHandle) sont pousses depuis la zone (voir
// matter_bridge_set_thermostat_presets(), appele par thermostat.cpp) : comme
// pour ElecPowerDelegate, le serveur CHIP ne fait que LIRE ces valeurs a la
// demande, c'est au delegate de signaler leurs changements
// (MatterReportingAttributeChangeCallback), sans quoi un controleur abonne
// (Home Assistant) ne les verrait jamais evoluer.
//
// Concurrence : toutes les methodes du Delegate (Get*/Append*/Commit*) sont
// invoquees par la tache CHIP, qui detient deja le "chip stack lock" pendant
// leur execution (meme verrou que lixee_chip_lock()/PlatformMgr().
// LockChipStack()) : elles n'ont donc PAS besoin de le prendre explicitement
// (lixee_chip_lock() y serait un no-op reconnu via
// IsChipStackLockedByCurrentThread()). Seules les methodes appelees depuis
// la tache des thermostats (un AUTRE thread : SyncFromZone(),
// GetCustomPresetsSnapshot()) doivent le prendre explicitement, pour se
// synchroniser avec la tache CHIP. Ces methodes-la ne doivent, a l'inverse,
// JAMAIS etre appelees en tenant le verrou des zones (s_lock, thermostat.cpp)
// : regulation_task prend s_lock PUIS lixee_chip_lock (ordre normal) ; le
// prendre dans l'autre sens cote CHIP ferait un deadlock. C'est pour ca que
// CommitPendingPresets() ne fait que notifier thermostat.cpp via une file
// (meme pattern que matter_write_cb/matter_preset_cb), jamais d'appel direct
// bloquant vers la zone.
// ============================================================================

#include <app/clusters/thermostat-server/thermostat-delegate.h>
#include <app/clusters/thermostat-server/thermostat-server.h>
#include <app/reporting/reporting.h>
#include <esp_matter_core.h>
#include <lib/core/CHIPError.h>     // CHIP_IM_GLOBAL_STATUS (issue #80 : rejet des presets perso invalides)
#include <lib/support/CodeUtils.h>
#include "elec_power_delegate.h"   // lixee_chip_lock()/lixee_chip_unlock() : esp-matter 1.5 ne publie plus lock::chip_stack_lock()
#include "matter_bridge.h"         // matter_preset_t, MATTER_PRESET_MAX_CUSTOM/NAME_LEN
#include <cstring>

class ThermostatPresetsDelegate : public chip::app::Clusters::Thermostat::Delegate
{
public:
    explicit ThermostatPresetsDelegate(int endpoint_id) : m_endpoint_id(endpoint_id) {}

    /**
     * Pousse l'etat courant de la zone (appele depuis la tache du thermostat
     * virtuel, voir matter_bridge_set_thermostat_presets()). Consignes en
     * centiemes de degre. active_custom_handle : 0 si Confort/Hors-gel est
     * actif (selon frost_active), sinon le handle du preset personnalise
     * actif. Signale les changements aux controleurs abonnes.
     */
    void SyncFromZone(int32_t comfort_setpoint, int32_t frost_setpoint, bool frost_active,
        const matter_preset_t *custom, int custom_count, uint8_t active_custom_handle)
    {
        auto clamp16 = [](int32_t v) -> int16_t {
            return (int16_t)(v < -27315 ? -27315 : (v > 32767 ? 32767 : v));
        };
        int16_t comfort = clamp16(comfort_setpoint);
        int16_t frost = clamp16(frost_setpoint);
        if (custom_count < 0) custom_count = 0;
        if (custom_count > MATTER_PRESET_MAX_CUSTOM) custom_count = MATTER_PRESET_MAX_CUSTOM;

        // Verrou pris avant de toucher les champs partages (lus par le
        // thread CHIP via GetPresetAtIndex()/GetActivePresetHandle()), pas
        // apres : sinon le thread CHIP peut lire un etat partiellement mis
        // a jour pendant la fenetre non protegee.
        bool held = lixee_chip_lock();
        bool active_changed = !m_synced || m_frost_active != frost_active ||
            m_active_custom_handle != active_custom_handle;
        bool presets_changed = m_comfort_sp != comfort || m_frost_sp != frost;
        // m_commit_pending (issue #80) : entre un CommitPendingPresets() (tache CHIP,
        // ecrit m_custom/m_custom_count) et le traitement de la notification associee
        // par thermostat.cpp (qui relit ce contenu via GetCustomPresetsSnapshot() --
        // seul endroit qui efface ce drapeau -- et le reinjecte dans la zone), un
        // SyncFromZone() periodique arrive ici avec l'ANCIEN tableau de la zone :
        // l'ecraser serait perdre l'edition qu'on vient de valider (verifie sur
        // materiel reel : suppression d'un preset perso silencieusement annulee).
        // On saute cette seule partie tant que la zone n'a pas rattrape le commit.
        if (!m_commit_pending) {
            if (m_custom_count != (uint8_t)custom_count) presets_changed = true;
            if (!presets_changed) {
                for (int i = 0; i < custom_count; i++) {
                    if (m_custom[i].handle != custom[i].handle ||
                        m_custom[i].heating_setpoint != custom[i].heating_setpoint ||
                        m_custom[i].scenario != custom[i].scenario ||
                        strncmp(m_custom[i].name, custom[i].name, MATTER_PRESET_NAME_LEN) != 0) {
                        presets_changed = true;
                        break;
                    }
                }
            }
            for (int i = 0; i < custom_count; i++) m_custom[i] = custom[i];
            m_custom_count = (uint8_t)custom_count;
        }
        m_comfort_sp = comfort;
        m_frost_sp = frost;
        m_frost_active = frost_active;
        m_active_custom_handle = active_custom_handle;
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

    /**
     * Lecture de la liste de presets personnalises courante, depuis la tache
     * des thermostats (voir matter_bridge_get_thermostat_custom_presets(),
     * appele apres une notification de commit). Prend lixee_chip_lock() :
     * jamais a appeler en tenant le verrou des zones (voir commentaire en
     * tete de fichier).
     */
    void GetCustomPresetsSnapshot(matter_preset_t *out, int max_out, int *out_count)
    {
        bool held = lixee_chip_lock();
        int n = (int)m_custom_count;
        if (n > max_out) n = max_out;
        for (int i = 0; i < n; i++) out[i] = m_custom[i];
        *out_count = n;
        m_commit_pending = false;   // la zone va rattraper ce contenu (voir SyncFromZone())
        lixee_chip_unlock(held);
    }

    // --- chip::app::Clusters::Thermostat::Delegate ---

    std::optional<chip::System::Clock::Milliseconds16> GetMaxAtomicWriteTimeout(chip::AttributeId) override
    {
        return chip::System::Clock::Milliseconds16(5000);   // edition des presets perso (issue #80)
    }

    // Capacite MAXIMALE supportee (voir thermostat-delegate.h : "The max number of
    // presets supported"), PAS le nombre de presets actuellement definis : deuxieme
    // bug de la meme famille que GetPresetTypeAtIndex() ci-dessous. L'ancien calcul
    // (kPresetCount + m_custom_count) renvoyait le compte courant -- 2 tant qu'aucun
    // preset perso n'existe -- ce qui faisait croire a un controleur (confirme avec
    // Home Assistant) que la capacite max etait deja atteinte, bloquant le tout
    // premier ajout avant meme d'essayer.
    uint8_t GetNumberOfPresets() override { return (uint8_t)(kPresetCount + MATTER_PRESET_MAX_CUSTOM); }

    CHIP_ERROR GetPresetTypeAtIndex(size_t index,
        chip::app::Clusters::Thermostat::Structs::PresetTypeStruct::Type &presetType) override
    {
        using chip::app::Clusters::Thermostat::PresetScenarioEnum;
        using chip::app::Clusters::Thermostat::PresetTypeFeaturesBitmap;
        // UNE SEULE entree par scenario (jamais deux pour le meme), quota perso inclus
        // pour Occupied/Vacation : thermostat-server-presets.cpp (MaximumPresetScenarioCount)
        // ne regarde que la PREMIERE entree dont le scenario correspond, pas la somme de
        // toutes -- confirme sur materiel reel (issue #80 bis) : avec deux entrees Occupied
        // separees (built-in quota=1, perso quota=3), demander un preset perso de scenario
        // Occupied trouvait l'entree built-in en premier (quota=1, deja consomme par
        // Confort) et echouait en ResourceExhausted, meme avec de la place reelle.
        struct Entry { PresetScenarioEnum scenario; uint8_t count; };
        static constexpr Entry kTypes[] = {
            // Occupied/Vacation : le preset fixe (Confort/Hors-gel) + jusqu'a
            // MATTER_PRESET_MAX_CUSTOM presets perso du meme scenario.
            { PresetScenarioEnum::kOccupied,     1 + MATTER_PRESET_MAX_CUSTOM },
            { PresetScenarioEnum::kVacation,     1 + MATTER_PRESET_MAX_CUSTOM },
            // Les autres scenarios n'ont pas de preset fixe : juste la capacite perso.
            { PresetScenarioEnum::kUnoccupied,   MATTER_PRESET_MAX_CUSTOM },
            { PresetScenarioEnum::kSleep,        MATTER_PRESET_MAX_CUSTOM },
            { PresetScenarioEnum::kWake,         MATTER_PRESET_MAX_CUSTOM },
            { PresetScenarioEnum::kGoingToSleep, MATTER_PRESET_MAX_CUSTOM },
            { PresetScenarioEnum::kUserDefined,  MATTER_PRESET_MAX_CUSTOM },
        };
        constexpr size_t kTypeCount = sizeof(kTypes) / sizeof(kTypes[0]);
        if (index >= kTypeCount) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        presetType.presetScenario = kTypes[index].scenario;
        presetType.numberOfPresets = kTypes[index].count;
        // SupportsNames : sans ca, certains controleurs (Home Assistant) n'exposent
        // pas le preset faute de nom a afficher.
        presetType.presetTypeFeatures = chip::BitMask<PresetTypeFeaturesBitmap>(PresetTypeFeaturesBitmap::kSupportsNames);
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetPresetAtIndex(size_t index,
        chip::app::Clusters::Thermostat::PresetStructWithOwnedMembers &preset) override
    {
        using namespace chip;
        using chip::app::Clusters::Thermostat::PresetScenarioEnum;
        if (index < kPresetCount) {
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
        size_t ci = index - kPresetCount;
        if (ci >= m_custom_count) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        const matter_preset_t &c = m_custom[ci];
        preset.SetPresetScenario((PresetScenarioEnum)c.scenario);
        ReturnErrorOnFailure(preset.SetPresetHandle(app::DataModel::MakeNullable(ByteSpan(&c.handle, 1))));
        CharSpan name = CharSpan::fromCharString(c.name);
        ReturnErrorOnFailure(preset.SetName(MakeOptional(app::DataModel::MakeNullable(name))));
        preset.SetCoolingSetpoint(NullOptional);
        preset.SetHeatingSetpoint(MakeOptional((int16_t)c.heating_setpoint));
        preset.SetBuiltIn(app::DataModel::MakeNullable(false));
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetActivePresetHandle(
        chip::app::DataModel::Nullable<chip::MutableByteSpan> &activePresetHandle) override
    {
        if (!m_synced) {
            activePresetHandle.SetNull();
            return CHIP_NO_ERROR;
        }
        uint8_t handle = m_active_custom_handle ? m_active_custom_handle : (m_frost_active ? kHandleFrost : kHandleComfort);
        ReturnErrorOnFailure(chip::CopySpanToMutableSpan(chip::ByteSpan(&handle, 1), activePresetHandle.Value()));
        activePresetHandle.Value().reduce_size(1);
        return CHIP_NO_ERROR;
    }

    // Appele quand un controleur invoque SetActivePresetRequest : repercute le
    // choix sur la zone (voir matter_bridge.cpp, qui a acces au callback
    // enregistre par thermostat.cpp). Definie hors ligne (matter_bridge.cpp).
    CHIP_ERROR SetActivePresetHandle(
        const chip::app::DataModel::Nullable<chip::ByteSpan> &newActivePresetHandle) override;

    // --- Edition des presets personnalises depuis un controleur (issue #80) ---

    void InitializePendingPresets() override { m_pending_count = 0; }

    CHIP_ERROR AppendToPendingPresetList(
        const chip::app::Clusters::Thermostat::PresetStructWithOwnedMembers &preset) override
    {
        using namespace chip;
        if (m_pending_count >= kMaxPending) return CHIP_ERROR_NO_MEMORY;

        auto handleSpan = preset.GetPresetHandle();
        uint8_t handle = 0;
        if (!handleSpan.IsNull() && handleSpan.Value().size() == 1) handle = handleSpan.Value().data()[0];

        matter_preset_t &slot = m_pending[m_pending_count];
        if (handle == kHandleComfort || handle == kHandleFrost) {
            // Normalement resoumis tel quel par le controleur (ReplaceAll) : contenu
            // ignore, le commit saute ces entrees (pilotees par la zone, voir
            // SyncFromZone()). Mais si le controleur soumet une consigne ou un nom
            // different de l'etat courant, c'est une tentative explicite de
            // modification de Confort/Hors-gel via ce flux -- refusee plutot
            // qu'ignoree silencieusement (revue PR #83), ces presets fixes n'etant
            // editables que depuis la zone elle-meme.
            bool frost = (handle == kHandleFrost);
            auto heatOpt = preset.GetHeatingSetpoint();
            if (heatOpt.HasValue() && heatOpt.Value() != (frost ? m_frost_sp : m_comfort_sp)) {
                return CHIP_IM_GLOBAL_STATUS(ConstraintError);
            }
            auto nameOpt = preset.GetName();
            if (nameOpt.HasValue() && !nameOpt.Value().IsNull()) {
                CharSpan expectedName = frost ? CharSpan::fromCharString("Hors-gel") : CharSpan::fromCharString("Confort");
                if (!nameOpt.Value().Value().data_equal(expectedName)) {
                    return CHIP_IM_GLOBAL_STATUS(ConstraintError);
                }
            }
            slot.handle = handle;
            slot.name[0] = '\0';
            slot.heating_setpoint = 0;
            slot.scenario = 0;
            m_pending_count++;
            return CHIP_NO_ERROR;
        }

        // Preset personnalise (nouveau si handle encore a 0, edite sinon) : nom et
        // consigne obligatoires, bornes reprises de thermostat.cpp (clamp32(sp,700,3000)
        // pousse vers l'attribut HeatingSetpoint) ; EnforceHeatingSetpointLimits() cote
        // SDK esp_matter vendore ici modifie une copie locale sans effet reel sur
        // cette version, d'ou la validation faite ici cote delegate (revue PR #83).
        auto nameOpt = preset.GetName();
        if (!nameOpt.HasValue() || nameOpt.Value().IsNull() || nameOpt.Value().Value().empty()) {
            return CHIP_IM_GLOBAL_STATUS(ConstraintError);
        }
        CharSpan nameSpan = nameOpt.Value().Value();
        if (nameSpan.size() >= MATTER_PRESET_NAME_LEN) return CHIP_IM_GLOBAL_STATUS(ConstraintError);
        char name[MATTER_PRESET_NAME_LEN];
        memcpy(name, nameSpan.data(), nameSpan.size());
        name[nameSpan.size()] = '\0';
        if (!strcmp(name, "Confort") || !strcmp(name, "Hors-gel")) return CHIP_IM_GLOBAL_STATUS(ConstraintError);
        for (uint8_t i = 0; i < m_pending_count; i++) {
            if (!strcmp(m_pending[i].name, name)) return CHIP_IM_GLOBAL_STATUS(ConstraintError);
        }

        auto heatOpt = preset.GetHeatingSetpoint();
        if (!heatOpt.HasValue() || heatOpt.Value() < 700 || heatOpt.Value() > 3000) {
            return CHIP_IM_GLOBAL_STATUS(ConstraintError);
        }

        slot.handle = handle;   // 0 : nouveau preset, handle attribue au commit
        strncpy(slot.name, name, MATTER_PRESET_NAME_LEN);
        slot.heating_setpoint = heatOpt.Value();
        slot.scenario = (uint8_t)preset.GetPresetScenario();
        m_pending_count++;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetPendingPresetAtIndex(size_t index,
        chip::app::Clusters::Thermostat::PresetStructWithOwnedMembers &preset) override
    {
        using namespace chip;
        using chip::app::Clusters::Thermostat::PresetScenarioEnum;
        if (index >= m_pending_count) return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        const matter_preset_t &p = m_pending[index];
        if (p.handle == kHandleComfort || p.handle == kHandleFrost) {
            bool frost = (p.handle == kHandleFrost);
            preset.SetPresetScenario(frost ? PresetScenarioEnum::kVacation : PresetScenarioEnum::kOccupied);
            ReturnErrorOnFailure(preset.SetPresetHandle(app::DataModel::MakeNullable(ByteSpan(&p.handle, 1))));
            CharSpan name = frost ? CharSpan::fromCharString("Hors-gel") : CharSpan::fromCharString("Confort");
            ReturnErrorOnFailure(preset.SetName(MakeOptional(app::DataModel::MakeNullable(name))));
            preset.SetCoolingSetpoint(NullOptional);
            preset.SetHeatingSetpoint(MakeOptional(frost ? m_frost_sp : m_comfort_sp));
            preset.SetBuiltIn(app::DataModel::MakeNullable(true));
            return CHIP_NO_ERROR;
        }
        preset.SetPresetScenario((PresetScenarioEnum)p.scenario);
        if (p.handle != 0) {
            ReturnErrorOnFailure(preset.SetPresetHandle(app::DataModel::MakeNullable(ByteSpan(&p.handle, 1))));
        } else {
            ReturnErrorOnFailure(preset.SetPresetHandle(app::DataModel::Nullable<ByteSpan>()));
        }
        CharSpan name = CharSpan::fromCharString(p.name);
        ReturnErrorOnFailure(preset.SetName(MakeOptional(app::DataModel::MakeNullable(name))));
        preset.SetCoolingSetpoint(NullOptional);
        preset.SetHeatingSetpoint(MakeOptional((int16_t)p.heating_setpoint));
        preset.SetBuiltIn(app::DataModel::MakeNullable(false));
        return CHIP_NO_ERROR;
    }

    // Definie hors ligne (matter_bridge.cpp) : notifie thermostat.cpp (file d'attente,
    // voir commentaire en tete de fichier) pour qu'il relise le resultat via
    // matter_bridge_get_thermostat_custom_presets() et le persiste.
    CHIP_ERROR CommitPendingPresets() override;

    void ClearPendingPresetList() override { m_pending_count = 0; }

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
    static constexpr uint8_t kMaxPending = kPresetCount + MATTER_PRESET_MAX_CUSTOM;

    int     m_endpoint_id;

    // --- Miroir partage avec la tache des thermostats : lixee_chip_lock() obligatoire
    //     cote tache des thermostats (SyncFromZone/GetCustomPresetsSnapshot), inutile
    //     cote tache CHIP (deja tenu, voir commentaire en tete de fichier). ---
    int16_t m_comfort_sp = 0;
    int16_t m_frost_sp = 0;
    bool    m_frost_active = false;
    uint8_t m_active_custom_handle = 0;      // 0 : Confort/Hors-gel actif (voir m_frost_active)
    bool    m_synced = false;                // aucun SyncFromZone() recu depuis la creation
    matter_preset_t m_custom[MATTER_PRESET_MAX_CUSTOM] = {};
    uint8_t m_custom_count = 0;
    // Mis a true par CommitPendingPresets() (tache CHIP), efface par
    // GetCustomPresetsSnapshot() (tache des thermostats, seul appelant : voir
    // matter_apply_preset_commit() dans thermostat.cpp) -- voir SyncFromZone().
    bool    m_commit_pending = false;

    // --- Buffer de la transaction d'edition en cours : tache CHIP uniquement
    //     (Initialize/Append/Commit/Clear), jamais touche par la tache des
    //     thermostats, pas de verrou necessaire. ---
    matter_preset_t m_pending[kMaxPending] = {};
    uint8_t m_pending_count = 0;
};
