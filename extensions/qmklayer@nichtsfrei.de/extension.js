/* extension.js */

import GObject from 'gi://GObject';
import Clutter from 'gi://Clutter';
import St from 'gi://St';
import Gio from 'gi://Gio';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

//const BUS_NAME = 'de.nichtsfrei.PanelMessage';
const BUS_NAME = null;
const OBJECT_PATH = '/de/nichtsfrei/PanelMessage';
const INTERFACE_NAME = 'de.nichtsfrei.PanelMessage';
const SIGNAL_NAME = 'Message';
const INITIAL_MESSAGE = 'L: Base';

const Indicator = GObject.registerClass(
class Indicator extends PanelMenu.Button {
    _init() {
        super._init(0.0, "L0");

        this._label = new St.Label({
            text: INITIAL_MESSAGE,
            style_class: 'system-status-icon',
            y_align: Clutter.ActorAlign.CENTER,
        });

        this.add_child(this._label);
    }

    setText(text) {
        this._label.set_text(text);
    }
});

export default class IndicatorExampleExtension extends Extension {
    enable() {
        this._indicator = new Indicator();
        Main.panel.addToStatusArea(this.uuid, this._indicator);

        this._signalId = Gio.DBus.session.signal_subscribe(
            BUS_NAME,          // sender (null = any)
            INTERFACE_NAME,    // interface
            SIGNAL_NAME,       // signal
            OBJECT_PATH,       // object path
            null,              // arg0
            Gio.DBusSignalFlags.NONE,
            this._onSignalReceived.bind(this)
        );
    }

    _onSignalReceived(conn, sender, objectPath, interfaceName, signalName, parameters) {
        try {
            const [message] = parameters.deepUnpack();
            this._indicator.setText(message);
        } catch (e) {
            log(`Failed to parse D-Bus signal: ${e}`);
        }
    }

    disable() {
        if (this._signalId) {
            Gio.DBus.session.signal_unsubscribe(this._signalId);
            this._signalId = null;
        }

        if (this._indicator) {
            this._indicator.destroy();
            this._indicator = null;
        }
    }
}
