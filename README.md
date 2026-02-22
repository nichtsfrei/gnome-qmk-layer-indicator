# Layer indicator

If you see this you're probably lost.

This is a small gnome-extension that shows the current active layer of my
keyboard.

Actually the extension shows whatever you send to:

```
gdbus emit --session  \
    --object-path /de/nichtsfrei/PanelMessage \
    --signal de.nichtsfrei.PanelMessage.Message \
    "narf"
```

But the lkbd binary tries to figure out if there is any device in
`/dev/hidraw*` tnat reacts to the command `L` by returning a byte array with
the first byte being `L` and the 31th byte the highest layer number.

If so then it listens on that device for layer-changes.

The corresponding keyboard modification looks like this:

```
static layer_state_t send_state(layer_state_t state) {
    uint8_t data[32] = {0};
    data[0]          = 'L';
    data[1]          = 1;
    data[31]         = get_highest_layer(state);
    raw_hid_send(data, 32);
    return state;
}

layer_state_t layer_state_set_user(layer_state_t state) {
  return send_state(update_tri_layer_state(state, FUNCTION, NUMBERS, MOUSE));
}

layer_state_t default_layer_state_set_user(layer_state_t state) {
    return send_state(state);
}


void raw_hid_receive(uint8_t *data, uint8_t length) {
    if (data[0] == 'L') {
        send_state(layer_state);
    }
}
```

If you are serious person that is triggered by that: please contact me. I am
always eager to learn but currently don't have the capacities to do right.

## Install extension

```
ln -s $(pwd)/extensions/qmklayer@nichtsfrei.de $HOME/.local/share/gnome-shell/extensions/qmklayer@nichtsfrei.de
```

## Install backend

```
cd lkb
nix build .#lkb 
install -m 755 result/bin/lkbd $HOME/.local/bin/
```

```
cp ./lkb/lkbd.service $HOME/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user start lkbd
systemctl --user enable lkbd
```
