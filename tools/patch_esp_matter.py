#!/usr/bin/env python3
"""Applique a chaque build des patchs locaux a espressif__esp_matter (managed_components/,
gitignore, retelecharge identique a chaque fetch de dependances -- voir CMakeLists.txt).

Idempotent : si le texte "new" est deja present, ne fait rien. Si ni "old" ni "new" ne
sont trouves (fichier vendore change, ex. montee de version esp-matter), echoue fort :
mieux vaut casser le build que laisser un contournement silencieusement perdu.
"""
import sys
from pathlib import Path

PATCHES = [
    {
        "name": "thermostat AtomicRequest sur endpoint dynamique (espressif/esp-matter#1928)",
        "path": "connectedhomeip/connectedhomeip/src/app/clusters/thermostat-server/thermostat-server-atomic.cpp",
        "old": """    for (size_t i = 0; i < index; ++i)
    {
        auto & attributeStatus = attributeStatuses[i];
        const EmberAfAttributeMetadata * metadata =
            emberAfLocateAttributeMetadata(endpoint, Thermostat::Id, attributeStatus.attributeID);

        if (metadata != nullptr)
        {
            // This is definitely an attribute we know about.
            continue;
        }

        if (IsSupportedGlobalAttributeNotInMetadata(attributeStatus.attributeID))
        {
            continue;
        }
""",
        "new": """    for (size_t i = 0; i < index; ++i)
    {
        auto & attributeStatus = attributeStatuses[i];

        // LOCAL PATCH (tools/patch_esp_matter.py, espressif/esp-matter#1928):
        // emberAfLocateAttributeMetadata() looks up the legacy Ember static attribute
        // table (emAfEndpoints[]), which esp-matter's dynamic endpoint model
        // (CodegenDataModelProvider, no ZAP) never populates -- confirmed by grepping
        // components/esp_matter/ for any call to emberAfSetDynamicEndpoint or
        // emAfEndpoints[]: none. So this lookup always returns nullptr for a
        // dynamically-created endpoint, making AtomicRequest permanently fail with
        // InvalidCommand for Presets/Schedules on any esp-matter dynamic bridge,
        // regardless of whether the attribute genuinely exists. CountAttributeRequests()
        // above already restricts attributeRequests to Presets::Id/Schedules::Id (the
        // only two atomic-quality attributes this whole mechanism handles), so
        // accepting them here without the broken lookup is safe.
        if (attributeStatus.attributeID == Presets::Id || attributeStatus.attributeID == Schedules::Id)
        {
            continue;
        }

        const EmberAfAttributeMetadata * metadata =
            emberAfLocateAttributeMetadata(endpoint, Thermostat::Id, attributeStatus.attributeID);

        if (metadata != nullptr)
        {
            // This is definitely an attribute we know about.
            continue;
        }

        if (IsSupportedGlobalAttributeNotInMetadata(attributeStatus.attributeID))
        {
            continue;
        }
""",
    },
]


def apply_patch(component_dir: Path, patch: dict) -> None:
    target = component_dir / patch["path"]
    name = patch["name"]
    if not target.is_file():
        sys.exit(f"patch_esp_matter.py: {name}: fichier introuvable ({target}) -- version d'esp-matter changee ?")

    text = target.read_text()
    if patch["new"] in text:
        return  # deja applique (re-build sans refetch des dependances)

    if patch["old"] not in text:
        sys.exit(
            f"patch_esp_matter.py: {name}: texte attendu introuvable dans {target} -- "
            "le fichier vendore a change (montee de version esp-matter ?), "
            "le patch doit etre revu a la main avant de continuer."
        )

    target.write_text(text.replace(patch["old"], patch["new"], 1))
    print(f"patch_esp_matter.py: applique : {name}")


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: patch_esp_matter.py <chemin_du_composant_espressif__esp_matter>")
    component_dir = Path(sys.argv[1])
    for patch in PATCHES:
        apply_patch(component_dir, patch)


if __name__ == "__main__":
    main()
