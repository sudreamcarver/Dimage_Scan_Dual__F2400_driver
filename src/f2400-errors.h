#ifndef F2400_ERRORS_H
#define F2400_ERRORS_H
/* DS_Dual.dll 1000a6a9. Diagnostic classification only: a code of zero
 * must NEVER turn a failed SG_IO command into a successful transfer. */
static int f2400_vendor_error(unsigned flags18, unsigned flags19, unsigned page_flags,
                              unsigned action, unsigned mode) {
    if (flags18 & 0x10)
        return 21;
    if (flags18 & 0x20)
        return 19;
    if (flags19 & 2)
        return 26;
    if (flags19 & 4)
        return 25;
    if (flags19 & 0x80)
        return 28;
    if (flags18 & 2)
        return 23;
    if (flags19 & 0x40)
        return 24;
    if (flags18 & 0x80)
        return flags18 & 0x40 ? 22 : page_flags & 0x10 ? 9 : 11;
    if (flags19 & 0x20)
        return 10;
    if ((flags18 & 0x49) || (flags19 & 9)) {
        /* Initialization has additional inquiry/page28 branches. Do not
         * interpret their special "continue setup" return as recovery. */
        if (action == 11)
            return -2;
        if (mode & 0x20) {
            if (!(page_flags & 0x10) || (flags18 & 0x40))
                return 36;
            if (flags18 & 1)
                return 8;
            if (flags19 & 1)
                return 31;
            if (flags18 & 8)
                return 29;
            if (flags19 & 8)
                return 30;
        } else {
            if (flags18 & 0x40)
                return 22;
            if (flags18 & 8)
                return page_flags & 0x10 ? 27 : 0;
            if ((flags18 & 1) || (flags19 & 9))
                return 27;
        }
    }
    return 1;
}
static const char *f2400_error_message(int code) {
    switch (code) {
    case 0:
        return "factory diagnostic continuation; transport error remains fatal";
    case 8:
        return "set film properly";
    case 9:
    case 11:
        return "setup pending; remove holder";
    case 10:
        return "selected frame does not exist";
    case 19:
        return "home position not confirmed";
    case 21:
        return "mechanism busy";
    case 22:
        return "35mm front panel open";
    case 23:
        return "APS adapter panel open";
    case 24:
        return "undeveloped APS film";
    case 25:
        return "film loading error";
    case 26:
        return "film rewind error";
    case 27:
        return "35mm holder/type mismatch";
    case 28:
        return "initial loading failed";
    case 29:
        return "APS state flag18.08; UI meaning unconfirmed";
    case 30:
        return "APS film already loaded";
    case 31:
        return "APS film rewinding";
    case 36:
        return "APS adapter/type mismatch";
    case -2:
        return "initialization-specific error branch; no automatic reset";
    default:
        return "unclassified device error; no command retry";
    }
}
#endif
