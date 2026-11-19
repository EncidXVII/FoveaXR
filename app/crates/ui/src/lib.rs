//! Tray app views. Colours, type sizes and spacing from design-tokens.json (desktop scale): 2 weights, 1 accent,
//! spacing 4/8/12/16/24. Main view (6 items) and Games view.

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

    export struct GameRow { name: string, detail: string, action: string, can-act: bool, on: bool }

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

    component Action inherits Rectangle {
        in property <string> text;
        in property <bool> enabled: true;
        in property <bool> lit;
        callback clicked();
        height: Tokens.control;
        background: !root.enabled ? transparent : root.lit ? Tokens.accent : touch.has-hover ? Tokens.line : Tokens.raised;
        border-width: root.enabled ? 0 : 1px;
        border-color: Tokens.line;
        accessible-role: button;
        accessible-label: root.text;
        Text {
            text: root.text;
            font-size: Tokens.body;
            font-weight: 600;
            color: !root.enabled ? Tokens.ink2 : root.lit ? Tokens.on-accent : Tokens.ink;
            horizontal-alignment: center;
            vertical-alignment: center;
        }
        touch := TouchArea {
            enabled: root.enabled;
            clicked => { root.clicked(); }
        }
    }

    component Labelled inherits VerticalLayout {
        in property <string> label;
        spacing: 8px;
        Text { text: root.label; font-size: Tokens.small; color: Tokens.ink2; }
        @children
    }

    export component MainWindow inherits Window {
        in-out property <int> tab;       // 0 Main, 1 Games
        in-out property <bool> enabled: true;
        in-out property <int> mode;      // 0 Fixed, 1 Eye-tracked
        in-out property <int> preset: 1; // 0 Low, 1 Medium, 2 High
        in-out property <bool> tint;
        in property <string> status: "No game running";
        in property <string> note;
        in property <[GameRow]> games;
        in property <string> games-status: "Looking for VR games…";
        callback changed();
        callback export-logs();
        callback game-action(int);

        title: "FoveatedVR";
        preferred-width: 400px;
        preferred-height: 620px;
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
            Segmented {
                options: ["Main", "Games"];
                selected <=> root.tab;
            }
            if root.tab == 0: VerticalLayout {
                spacing: 16px;
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
                Labelled {
                    label: "Mode";
                    Segmented {
                        options: ["Fixed", "Eye-tracked"];
                        selected <=> root.mode;
                        changed => { root.changed(); }
                    }
                }
                Labelled {
                    label: "Preset";
                    Segmented {
                        options: ["Low", "Medium", "High"];
                        selected <=> root.preset;
                        changed => { root.changed(); }
                    }
                }
                Labelled {
                    label: "Show the rings in VR (debug tint)";
                    Segmented {
                        options: ["Off", "On"];
                        selected: root.tint ? 1 : 0;
                        changed => {
                            root.tint = self.selected == 1;
                            root.changed();
                        }
                    }
                }
                Text {
                    text: root.status;
                    font-size: Tokens.body;
                    color: Tokens.ink;
                    wrap: word-wrap;
                }
                Text {
                    text: "Ctrl+F9 in VR: compare on and off. Ctrl+F10: Off, Fixed, Eye-tracked.";
                    font-size: Tokens.small;
                    color: Tokens.ink2;
                    wrap: word-wrap;
                }
                Rectangle { vertical-stretch: 1; }
                HorizontalLayout {
                    spacing: 12px;
                    Action {
                        width: 140px;
                        text: "Export logs";
                        clicked => { root.export-logs(); }
                    }
                    Text {
                        text: root.note;
                        font-size: Tokens.small;
                        color: Tokens.ink2;
                        vertical-alignment: center;
                        wrap: word-wrap;
                    }
                }
            }
            if root.tab == 1: VerticalLayout {
                spacing: 12px;
                Text { text: root.games-status; font-size: Tokens.small; color: Tokens.ink2; wrap: word-wrap; }
                Flickable {
                    vertical-stretch: 1;
                    VerticalLayout {
                        spacing: 8px;
                        alignment: start;
                        for game[i] in root.games: HorizontalLayout {
                            spacing: 12px;
                            VerticalLayout {
                                alignment: center;
                                Text { text: game.name; font-size: Tokens.body; color: Tokens.ink; overflow: elide; }
                                Text { text: game.detail; font-size: Tokens.small; color: Tokens.ink2; overflow: elide; }
                            }
                            Action {
                                width: 112px;
                                text: game.action;
                                enabled: game.can-act;
                                lit: game.on;
                                clicked => { root.game-action(i); }
                            }
                        }
                    }
                }
            }
        }
    }
}
