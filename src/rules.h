#include "config.h"
void jsonToRules(Rule* rules, int& ruleCount);
bool evaluateCondition(const RuleCondition& condition);
double getCurrentValue(const char* type,int cluster, int attribute, const char* IEEE);
void applyRules(Rule* rules, int ruleCount);
int getStatusRule(const char* name);
String getLastDateRule(const char* name);