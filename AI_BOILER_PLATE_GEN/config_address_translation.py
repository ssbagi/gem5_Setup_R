from m5.objects import AddressTranslation


translation = AddressTranslation(
    page_bits=12,
    physical_offset=0x1000,
    tlb_entries=64,
    hit_latency=4,
    miss_latency=30,
)

print(translation)