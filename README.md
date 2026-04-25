# 🍌 BananaOS v0.1

Un OS minimaliste x86 écrit from scratch (sans Linux ni kernel existant),
bootable dans VirtualBox via GRUB + Multiboot.

```
  ____                               ____  ____
 | __ )  __ _ _ __   __ _ _ __   __|  _ \/ ___|
 |  _ \ / _` | '_ \ / _` | '_ \ / _` | | \___  \
 | |_) | (_| | | | | (_| | | | | (_| | |___)  |
 |____/ \__,_|_| |_|\__,_|_| |_|\__,_|___/____/
```

## Structure

```
bananOS/
├── boot/
│   ├── boot.asm        # Entry point Multiboot en assembleur
│   └── linker.ld       # Script de linker
├── kernel/
│   ├── kernel.c        # kernel_main()
│   ├── terminal.c/h    # Driver VGA text 80x25
│   └── keyboard.c/h    # Driver PS/2 keyboard
├── shell/
│   └── shell.c/h       # Shell CLI (bsh)
├── iso/
│   └── boot/grub/
│       └── grub.cfg    # Config GRUB
├── Makefile
├── build.sh
└── README.md
```

## Commandes disponibles

| Commande | Description |
|----------|-------------|
| `help` | Affiche la liste des commandes |
| `neofetch` | Affiche les infos système style neofetch |
| `echo <texte>` | Affiche du texte |
| `clear` | Efface l'écran |
| `uname` | Affiche le nom du système |
| `keyboardctl [layout]` | Affiche ou change le layout clavier (`EN (Default)`, `fr_CH`, `FR`, `DE`, `de_CH`, `BEPO`) |
| `loadctl [layout]` | Alias de `keyboardctl` |
| `usbctl` | Affiche l'état du handoff USB legacy (xHCI/EHCI) |
| `ip` | Affiche la configuration réseau actuelle |
| `ip config <ip> <mask> <gateway> <dns1> <dns2>` | Configure le réseau IPv4 statique |
| `ping <ipv4>` | Lance des tests de connectivité de base |
| `netd [start|stop|status]` | Contrôle le daemon réseau simulé |
| `top` | Moniteur live avec table des processus et scheduler |
| `halt` | Arrête le système |

## Build (Ubuntu/Debian)

```bash
chmod +x build.sh
./build.sh
```

Ou manuellement :

```bash
sudo apt-get install nasm gcc-multilib grub-pc-bin grub-common xorriso
make
# → produit bananOS.iso
```

## Lancer dans VirtualBox

1. **Nouveau VM** → Nom: BananaOS, Type: Other, Version: Other/Unknown (32-bit)
2. **RAM**: 32 MB minimum
3. **Pas de disque dur** nécessaire
4. **Paramètres → Stockage** → ajouter `bananOS.iso` comme lecteur optique
5. **Démarrer** !

## Lancer dans QEMU (test rapide)

```bash
qemu-system-i386 -cdrom bananOS.iso
```

## Architecture technique

- **Bootloader** : GRUB 2 (Multiboot spec)
- **Assembleur** : NASM (entry point, stack setup)
- **Langage** : C freestanding (pas de stdlib, pas de libc)
- **VGA** : Accès direct à `0xB8000` (text mode 80×25)
- **Clavier** : Polling PS/2 port `0x60`/`0x64`, scancode set 1
- **Kernel** : Chargé à `0x100000` (1 MiB)

## Pas de kernel existant !

Ce projet n'utilise **aucun kernel Linux ou autre**. Tout est custom :
- Entry point en asm pur
- Driver VGA en C bare-metal
- Driver keyboard en C bare-metal  
- Shell maison
- Linked avec un linker script custom
