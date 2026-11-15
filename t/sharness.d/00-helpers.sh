##
# Extra assertions, in the style of sharness's own test_must_be_empty().
##

# Check that a file matches a pattern, and barf with the file's contents
# otherwise.  Use in place of grep when a failure is easier to understand
# if the actual text is shown, such as an error message that may
# legitimately vary.
#
# usage: test_grep pattern file
test_grep() {
    if ! grep "$1" "$2"
    then
        echo "'$2' does not match '$1', it contains:"
        cat "$2"
        return 1
    fi
}

##
# Kernel configuration
##

# Echo the running kernel's configuration, if available.  Cached.
#
# usage: get_kernel_config
get_kernel_config() {
    if test -z "$_kernel_config_read"; then
        _kernel_config_read=t
        if test -r /proc/config.gz; then
            _kernel_config=$(zcat /proc/config.gz 2>/dev/null)
        elif test -r /boot/config-$(uname -r); then
            _kernel_config=$(cat /boot/config-$(uname -r) 2>/dev/null)
        fi
    fi
    echo "$_kernel_config"
}

# Log kernel config options whose name matches one or more shell globs,
# with the kernel version, once per run.  A test that fails because of
# how the kernel was built then says so in the log.  Name options without
# their CONFIG_ prefix, as test_set_kernel_prereq() does; a glob that
# carries one anyway still works.
#
# usage: test_report_kernel_config '9P*' 'NET_9P*'
test_report_kernel_config() {
    local stamp=${SHARNESS_TEST_DIRECTORY:-.}/test-results/kernel-config.report
    local config
    local item
    local name
    local glob

    # One copy per 'make check': first script here writes the stamp.
    # Returns non-zero if another script already reported, so that a
    # caller can skip related output of its own.
    mkdir -p $(dirname $stamp) 2>/dev/null
    test -e $stamp && return 1
    : >$stamp 2>/dev/null

    say "kernel: $(uname -r)"
    config=$(get_kernel_config)
    if test -z "$config"; then
        say "kernel config: not readable"
        return 0
    fi
    echo "$config" | while read -r item; do
        case "$item" in
            CONFIG_*) ;;
            *) continue ;;
        esac
        name=${item#CONFIG_}
        name=${name%%=*}
        for glob in "$@"; do
            case "$name" in
                $glob|${glob#CONFIG_})
                    say "kernel config: $item"; break ;;
            esac
        done
    done
    return 0
}

# Set a prereq of the same name if a kernel config option is built in or
# a module.  Name the option without its CONFIG_ prefix.  An unreadable
# kernel config sets no prereq, so gated tests skip rather than fail.
# Gate a test on the option being unset with sharness negation, as in
# test_expect_success '!9P_FS_SECURITY'.
#
# usage: test_set_kernel_prereq 9P_FS_SECURITY
test_set_kernel_prereq() {
    local name=$1

    case "$name" in
        "")
            error "test_set_kernel_prereq: missing config option name" ;;
        CONFIG_*)
            error "test_set_kernel_prereq: use ${name#CONFIG_}," \
                  "not ${name}" ;;
    esac

    case "$(get_kernel_config | sed -n "s/^CONFIG_$name=//p")" in
        y|m) test_set_prereq "$name" ;;
    esac
}
