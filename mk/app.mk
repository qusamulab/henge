APP_DD  := $(B)/app/dd
APP_OUT := $(APP_DD)/Build/Products/Debug-iphonesimulator/henge.app

# ---- iOS host app (app/) ----
# Converted libadd.dylib for the app bundle. Left unsigned: Xcode embeds it into
# Frameworks/ and signs it with the app's identity (CodeSignOnCopy).
# Called from the app's pre-build phase (scripts/app-prebuild.sh).
app-dylib: $(B)/samples/libadd.so $(B)/henge-convert ## convert libadd.so for the app (PLATFORM=iossim|ios)
	@case "$(PLATFORM)" in iossim|ios) ;; *) echo "PLATFORM must be iossim or ios" >&2; exit 1;; esac
	@mkdir -p $(B)/app
	$(B)/henge-convert --platform $(PLATFORM) $(CONVERT_FLAGS) -m $(B)/app/libadd.manifest.json -o $(B)/app/libadd.dylib $<

# Simulator build, ad-hoc signed ("Sign to Run Locally"), so no Apple team or
# app/Local.xcconfig is needed. NDK_ROOT is passed through to the pre-build phase.
app: ## generate the Xcode project and build the app for the simulator
	cd app && xcodegen --quiet
	NDK_ROOT="$(NDK_ROOT)" xcodebuild -project app/henge.xcodeproj -scheme henge -configuration Debug \
	  -destination 'platform=iOS Simulator,name=$(SIM)' -derivedDataPath $(APP_DD) \
	  CODE_SIGN_STYLE=Manual CODE_SIGN_IDENTITY=- DEVELOPMENT_TEAM= \
	  -quiet build
	@echo "built $(APP_OUT)"

app-run: ## boot SIM if needed, install + launch the app, require "PASS: m1 (0 failures)"
	scripts/app-run.sh "$(APP_OUT)" "$(SIM)" "$(APP_TIMEOUT)"
