# shellcheck shell=bash
# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Wraps the utility built on macOS in an application bundle, signed ad hoc.
# Contributors: None
#
# Sourced, never run, by deploy/build-utility.sh on macOS alone, as only a Mac builds the utility for macOS
# and only its codesign signs one: deploy/build-utility.ps1 has no part of this, and says so.
#
# A program is a Mac's own only inside a bundle: with its name and icon in the Dock, and the questions macOS
# asks before it reaches a garland on the local network or reads its token from the keychain asked of it,
# not of the terminal that started it. So on macOS the build's file, which its manifest names and a release
# publishes, is the bundle zipped, csiGarland.app in it, by csg_tool bundle-zip: the same program, icon and
# version make the same bundle, and the same zip, to the byte. It is signed ad hoc, by no one: the project
# has no Developer ID, so macOS asks once, on the first start of a bundle downloaded, whether to open it, as
# docs/guides/utility.md says.

# The bundle's identifier: what macOS keeps the owner's answers and the keychain's items by. Never change it,
# or every Mac asks them all again.
CSG_MACOS_BUNDLE_ID='am.csilab.csigarland'
# What macOS 12 runs, the oldest Go supports.
CSG_MACOS_MINIMUM='12.0'

# csg_macos_bundle <program> <bundle> <version> <bundle version> <short version> <icon>: makes the bundle,
# such as csiGarland.app, from the program built, replacing one there. The version is the build's own, such
# as 1.3.0.27-dev, the bundle version its plain numbers, and the short version its base, such as 1.3.0.
csg_macos_bundle() {
    local file="$1" bundle="$2" version="$3" bundle_version="$4" short_version="$5" icon="$6"
    local contents="$bundle/Contents"
    [ -f "$file" ] || csg_die "No $file to make a bundle of."
    [ -f "$icon" ] || csg_die "No icon $icon for the bundle: make it with cmd/csigarland/generate_icon.go."
    command -v codesign >/dev/null 2>&1 ||
        csg_die 'codesign is not here, so the bundle cannot be signed: install the Xcode command line tools, xcode-select --install.'
    rm -rf "$bundle"
    mkdir -p "$contents/MacOS" "$contents/Resources" || csg_die "Could not make $bundle."
    cp -f "$file" "$contents/MacOS/csigarland" || csg_die "Could not put the utility in $bundle."
    cp -f "$icon" "$contents/Resources/csigarland.icns" || csg_die "Could not put the icon in $bundle."
    printf 'APPL????' >"$contents/PkgInfo"
    cat >"$contents/Info.plist" <<EOF || csg_die "Could not write $contents/Info.plist."
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>en</string>
	<key>CFBundleDisplayName</key>
	<string>csiGarland</string>
	<key>CFBundleExecutable</key>
	<string>csigarland</string>
	<key>CFBundleGetInfoString</key>
	<string>csiGarland $version</string>
	<key>CFBundleIconFile</key>
	<string>csigarland</string>
	<key>CFBundleIdentifier</key>
	<string>$CSG_MACOS_BUNDLE_ID</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>csiGarland</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>$short_version</string>
	<key>CFBundleVersion</key>
	<string>$bundle_version</string>
	<key>LSApplicationCategoryType</key>
	<string>public.app-category.utilities</string>
	<key>LSMinimumSystemVersion</key>
	<string>$CSG_MACOS_MINIMUM</string>
	<key>NSHighResolutionCapable</key>
	<true/>
	<key>NSHumanReadableCopyright</key>
	<string>Copyright (c) 2026 Anton Bespalov. MIT License.</string>
	<key>NSLocalNetworkUsageDescription</key>
	<string>csiGarland looks for your garland on this network, and talks to it at its address.</string>
	<key>NSSupportsAutomaticGraphicsSwitching</key>
	<true/>
</dict>
</plist>
EOF
    plutil -lint "$contents/Info.plist" >/dev/null || csg_die "$contents/Info.plist is not a property list."
    codesign --force --sign - --identifier "$CSG_MACOS_BUNDLE_ID" --timestamp=none "$bundle" >/dev/null 2>&1 ||
        csg_die "codesign could not sign $bundle."
    codesign --verify --strict "$bundle" >/dev/null 2>&1 || csg_die "The signature of $bundle does not hold."
}

# csg_macos_unzip_beside <zip> <into>: the bundle a build's zip holds, put at <into>, such as the zip's path
# with .app for .zip, replacing one there, for deploy/run.sh to start: what is started is what is published.
csg_macos_unzip_beside() {
    local zip="$1" into="$2" unpacked
    mkdir -p "$CSG_REPO_ROOT/temp" || csg_die "Could not make $CSG_REPO_ROOT/temp."
    unpacked="$(mktemp -d "$CSG_REPO_ROOT/temp/utility-app.XXXXXX")" || csg_die 'Could not make a folder to unzip the bundle in.'
    if ! ditto -x -k "$zip" "$unpacked" || [ ! -d "$unpacked/csiGarland.app" ]; then
        rm -rf "$unpacked"
        csg_die "$zip holds no csiGarland.app."
    fi
    rm -rf "$into"
    mv "$unpacked/csiGarland.app" "$into" || csg_die "Could not put the bundle at $into."
    rm -rf "$unpacked"
}
