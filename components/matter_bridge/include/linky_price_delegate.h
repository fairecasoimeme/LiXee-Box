#pragma once

// ============================================================================
// Delegate pour le cluster standard CommodityPrice (0x0095) sur l'EP Linky.
//
// Contrairement a WaterHeaterManagementDelegate (voir water_heater_delegate.h),
// ce Delegate est un simple placeholder : toute la logique (lecture des
// attributs, commandes GetDetailedPriceRequest/GetDetailedForecastRequest) est
// deja implementee par CommodityPrice::Instance (AttributeAccessInterface +
// CommandHandlerInterface, voir
// connectedhomeip/src/app/clusters/commodity-price-server/). Le pont se
// contente d'appeler Instance::SetCurrentPrice()/SetCurrency()/SetTariffUnit()
// depuis matter_bridge_report_tariff_price() -- meme principe que
// matter_bridge_report_energy_index() pour ElectricalEnergyMeasurement.
// ============================================================================

#include <app/clusters/commodity-price-server/commodity-price-server.h>

class LinkyPriceDelegate : public chip::app::Clusters::CommodityPrice::Delegate
{
public:
    LinkyPriceDelegate() = default;
};
