"""Sungrow energy flow: the ESPHome side.

The five energy counters of plugin.yaml are handed to the component as texts: the card names them when it asks Home
Assistant for their daily changes. smart_display.register_plugin() reads the id, version, tile memory and the screen's
texts from the manifest and translations/ beside this folder.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display"]

# In the order the component keeps them (sungrow_flow.h, Counter).
COUNTERS = ["total_pv", "total_import", "total_export", "total_charge", "total_discharge"]

sungrow_flow_ns = cg.esphome_ns.namespace("sungrow_flow")
SungrowFlow = sungrow_flow_ns.class_("SungrowFlow", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SungrowFlow),
        **{cv.Optional(name, default=""): cv.string for name in COUNTERS},
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
    for index, name in enumerate(COUNTERS):
        cg.add(var.set_counter(index, config[name]))
