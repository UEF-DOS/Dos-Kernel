# Kernel build configuration.
#
# Set options here and then run `make` from the repository root.
# Example:
#   CONFIG_DEBUG = y
#   CONFIG_APIC_TIMER = y

# Make sure to run `make clean` after changing options here to ensure a full rebuild with the new settings.

# Enable extra debug output
CONFIG_DEBUG ?= n

# Enable the APIC timer calibration
CONFIG_APIC_TIMER ?= y

# Set the desired frequency for the APIC in Hz
CONFIG_APIC_TIMER_FREQUENCY ?= 100