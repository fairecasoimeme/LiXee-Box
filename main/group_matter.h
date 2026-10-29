#pragma once

// Groupes d'actions publies dans Matter (interrupteurs virtuels) - group_matter.cpp

/** Demarrage, apres matter_bridge_start() : cree les interrupteurs des groupes publies. */
void group_matter_start(void);

/** Resynchronise les interrupteurs (groupes modifies, appareil retire / remis dans Matter). */
void group_matter_refresh(void);
