SHELL := /usr/bin/env bash
include versions.lock

.PHONY: doctor bootstrap components sdk release verify clean help

help:
	@printf '%s\n' \
	  'AirLink $(AIRLINK_VERSION) ($(AIRLINK_BUILD)) build targets:' \
	  '  make doctor      Check WSL2/Ubuntu/resources and source integrity' \
	  '  make bootstrap   Install Ubuntu packages and fetch host-tools' \
	  '  make components  Build and test C906L, airlinkd and airlinkctl' \
	  '  make sdk         Clean-build SG2002 Kernel/DTB/RootFS/FIP from source' \
	  '  make release     Build all components and assemble the release image' \
	  '  make verify      Verify the release image and generate reports' \
	  '  make clean       Remove AirLink-generated out/ files'

doctor:
	@./scripts/doctor.sh

bootstrap:
	@./scripts/bootstrap.sh

components:
	@./scripts/doctor.sh --strict
	@./scripts/build_components.sh

sdk:
	@./scripts/doctor.sh --strict
	@./scripts/build_sdk.sh

release:
	@./scripts/release.sh

verify:
	@./scripts/verify_release.sh
	@./scripts/generate_validation_evidence.sh

clean:
	@./scripts/clean.sh
