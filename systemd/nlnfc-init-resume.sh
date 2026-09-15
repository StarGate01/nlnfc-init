#!/bin/sh
#
# systemd-logind sleep hook: re-apply the NPC300 proprietary NCI
# configuration after resume. The controller loses its volatile
# configuration across suspend, so it must be re-primed before pcscd (or
# anything else) touches it again.
#
# Install into /usr/lib/systemd/system-sleep/ (root-owned, mode 0755).
# See: man systemd-suspend.service (system sleep hooks)

case "$1/$2" in
	post/suspend|post/hibernate|post/hybrid-sleep|post/suspend-then-hibernate)
		/usr/local/bin/nlnfc-init --quiet || true
		;;
esac

exit 0
