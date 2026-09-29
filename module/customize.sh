if [ "${ARCH:-}" != arm64 ]; then
    abort "Xbox Bluetooth Rumble Bridge requires arm64"
fi

# The script is sourced by all three module installers. Only name a policy
# domain that exists in the manager performing this installation.
if [ "${KSU:-}" = true ]; then
    : # Packaged ksu rules apply.
elif [ "${APATCH:-}" = true ]; then
    context=$(id -Z)
    domain=${context#u:r:}
    domain=${domain%%:*}
    case "$domain" in
        ''|*[!a-zA-Z0-9_]*) abort "Could not determine APatch service SELinux domain" ;;
    esac
    sed "s/^allow ksu /allow $domain /" "$MODPATH/sepolicy.rule" > "$MODPATH/sepolicy.next"
    mv "$MODPATH/sepolicy.next" "$MODPATH/sepolicy.rule"
else
    # Magisk's service process uses the magisk domain, which its own policy
    # grants broad device access. A rule naming the absent ksu type would fail.
    : > "$MODPATH/sepolicy.rule"
fi

set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/xbox_bridge" 0 0 0755
