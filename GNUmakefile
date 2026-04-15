# I hate makefiles so this is gonna be done by AI
.SUFFIXES:

ARCH        := x86_64
QEMUFLAGS   := -m 2G -debugcon stdio -no-reboot -no-shutdown
IMAGE_NAME  := template-$(ARCH)

HOST_CC      := cc
HOST_CFLAGS  := -g -O2 -pipe
HOST_CPPFLAGS :=
HOST_LDFLAGS :=
HOST_LIBS    :=

# ── Phony targets ────────────────────────────────────────────────────────────
.PHONY: all all-hdd run run-x86_64 kernel clean distclean create-disks

all: $(IMAGE_NAME).iso

all-hdd: $(IMAGE_NAME).hdd

run: run-$(ARCH)

# ── Disk image (only created once, not on every run) ─────────────────────────
disks/ahci.hdd:
	mkdir -p disks
	dd if=/dev/zero bs=1M count=32 of=disks/ahci.hdd
	mformat -i disks/ahci.hdd -t 64 -h 16 -s 63 -S 2 -M 512

# Convenience target to force-recreate the disk
create-disks:
	rm -f disks/ahci.hdd
	$(MAKE) disks/ahci.hdd

# ── QEMU run ─────────────────────────────────────────────────────────────────
run-x86_64: edk2-ovmf disks/ahci.hdd $(IMAGE_NAME).iso
	qemu-system-$(ARCH) \
		-M q35 \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-$(ARCH).fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
		-device ahci,id=ahci \
		-drive if=none,id=ahcidisk,format=raw,file=disks/ahci.hdd \
		-device ide-hd,drive=ahcidisk,bus=ahci.0 \
		$(QEMUFLAGS)

# ── External deps ─────────────────────────────────────────────────────────────
edk2-ovmf:
	curl -L https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/edk2-ovmf.tar.gz \
		| gunzip | tar -xf -

limine/limine:
	rm -rf limine
	git clone https://github.com/limine-bootloader/limine.git limine \
		--branch=v8.x-binary --depth=1
	$(MAKE) -C limine \
		CC="$(HOST_CC)" \
		CFLAGS="$(HOST_CFLAGS)" \
		CPPFLAGS="$(HOST_CPPFLAGS)" \
		LDFLAGS="$(HOST_LDFLAGS)" \
		LIBS="$(HOST_LIBS)"

kernel/.deps-obtained:
	./kernel/get-deps

# ── Kernel ────────────────────────────────────────────────────────────────────
kernel: kernel/.deps-obtained
	$(MAKE) -C kernel

# ── ISO image ─────────────────────────────────────────────────────────────────
$(IMAGE_NAME).iso: limine/limine kernel
	rm -rf iso_root
	mkdir -p iso_root/boot/limine iso_root/EFI/BOOT
	cp -v kernel/bin-$(ARCH)/kernel iso_root/boot/
	cp -v limine.conf iso_root/boot/limine/
ifeq ($(ARCH),x86_64)
	cp -v limine/limine-bios.sys \
	      limine/limine-bios-cd.bin \
	      limine/limine-uefi-cd.bin \
	      iso_root/boot/limine/
	cp -v limine/BOOTX64.EFI  iso_root/EFI/BOOT/
	cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
	./limine/limine bios-install $(IMAGE_NAME).iso
endif
	rm -rf iso_root

# ── HDD image ─────────────────────────────────────────────────────────────────
$(IMAGE_NAME).hdd: limine/limine kernel
	rm -f $(IMAGE_NAME).hdd
	dd if=/dev/zero bs=1M count=64 of=$(IMAGE_NAME).hdd
	PATH=$$PATH:/usr/sbin:/sbin sgdisk $(IMAGE_NAME).hdd -n 1:2048 -t 1:ef00
	./limine/limine bios-install $(IMAGE_NAME).hdd
	mformat -i $(IMAGE_NAME).hdd@@1M -t 62 -h 16 -s 63 -R 32
	mmd    -i $(IMAGE_NAME).hdd@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
	mcopy  -i $(IMAGE_NAME).hdd@@1M kernel/bin-$(ARCH)/kernel  ::/boot
	mcopy  -i $(IMAGE_NAME).hdd@@1M limine.conf                ::/boot/limine
	mcopy  -i $(IMAGE_NAME).hdd@@1M limine/limine-bios.sys     ::/boot/limine
	mcopy  -i $(IMAGE_NAME).hdd@@1M limine/BOOTX64.EFI         ::/EFI/BOOT
	mcopy  -i $(IMAGE_NAME).hdd@@1M limine/BOOTIA32.EFI        ::/EFI/BOOT

# ── Cleanup ───────────────────────────────────────────────────────────────────
clean:
	$(MAKE) -C kernel clean
	rm -rf iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).hdd disks/

distclean:
	$(MAKE) -C kernel distclean
	rm -rf iso_root *.iso *.hdd limine edk2-ovmf disks/