# Copyright (c) 2026, gem5 Contributors

from m5.params import *
from m5.SimObject import SimObject


class AddressTranslation(SimObject):
    type = "AddressTranslation"
    cxx_header = "AI_BOILER_PLATE_GEN/address_translation.hh"
    cxx_class = "gem5::AddressTranslation"

    page_bits = Param.Unsigned(12, "Log2 of the page size")
    physical_offset = Param.Addr(0x1000, "Page-number offset for the map")
    tlb_entries = Param.Unsigned(16, "Number of direct-mapped TLB entries")
    hit_latency = Param.Cycles(4, "Latency for a TLB hit")
    miss_latency = Param.Cycles(30, "Latency for a TLB miss")