//! Tray app views. Colours, type sizes and spacing from design-tokens.json (desktop scale): 2 weights, 1 accent,
//! spacing 4/8/12/16/24. This phase has the main view; the games, advanced and first-run views follow with the app.

slint::slint! {
    #[edition = "1.19"]
    import "../fonts/Barlow-Regular.ttf";
    import "../fonts/Barlow-SemiBold.ttf";

    global Tokens {
        out property <color> bg: #0B0B0C;
        out property <color> raised: #1D1D20;
        out property <color> line: #2A2A2E;
        out property <color> ink: #F5F5F7;
        out property <color> ink2: #A8A8AF;
        out property <color> accent: #C6FF1A;
        out property <color> on-accent: #0B0B0C;
        out property <length> heading: 24px;
        out property <length> body: 17px;
        out property <length> small: 15px;
        out property <length> control: 44px;
    }

    component Segmented inherits HorizontalLayout {
        in property <[string]> options;
        in-out property <int> selected;
        callback changed();
        spacing: 4px;
        for option[i] in root.options: Rectangle {
            height: Tokens.control;
            horizontal-stretch: 1;
            background: i == root.selected ? Tokens.accent : touch.has-hover ? Tokens.line : Tokens.raised;
            accessible-role: button;
            accessible-label: option;
            Text {
                text: option;
                font-size: Tokens.body;
                font-weight: 600;
                color: i == root.selected ? Tokens.on-accent : Tokens.ink;
                horizontal-alignment: center;
                vertical-alignment: center;
            }
            touch := TouchArea {
                clicked => {
                    root.selected = i;
                    root.changed();
                }
            }
        }
    }

    export component MainWindow inherits Window {
        in-out property <bool> enabled: true;
        in-out property <int> mode;    // 0 Fixed, 1 Eye-tracked
        in-out property <int> preset: 1;  // 0 Low, 1 Medium, 2 High
        in property <string> status: "GPU saved: no game running";
        callback changed();

        title: "FoveatedVR";
        preferred-width: 360px;
        background: Tokens.bg;
        default-font-family: "Barlow";

        VerticalLayout {
            padding: 24px;
            spacing: 16px;
            Text {
                text: "FoveatedVR";
                font-size: Tokens.heading;
                font-weight: 600;
                color: Tokens.ink;
            }
            Rectangle {
                height: 64px;
                background: root.enabled ? Tokens.accent : power.has-hover ? Tokens.line : Tokens.raised;
                accessible-role: switch;
                accessible-label: "Foveated rendering";
                accessible-checked: root.enabled;
                Text {
                    text: root.enabled ? "On" : "Off";
                    font-size: Tokens.heading;
                    font-weight: 600;
                    color: root.enabled ? Tokens.on-accent : Tokens.ink;
                }
                power := TouchArea {
                    clicked => {
                        root.enabled = !root.enabled;
                        root.changed();
                    }
                }
            }
            VerticalLayout {
                spacing: 8px;
                Text { text: "Mode"; font-size: Tokens.small; color: Tokens.ink2; }
                Segmented {
                    options: ["Fixed", "Eye-tracked"];
                    selected <=> root.mode;
                    changed => { root.changed(); }
                }
            }
            VerticalLayout {
                spacing: 8px;
                Text { text: "Preset"; font-size: Tokens.small; color: Tokens.ink2; }
                Segmented {
                    options: ["Low", "Medium", "High"];
                    selected <=> root.preset;
                    changed => { root.changed(); }
                }
            }
            Text { text: root.status; font-size: Tokens.body; color: Tokens.ink2; }
        }
    }
}
