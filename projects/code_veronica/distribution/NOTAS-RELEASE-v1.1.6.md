## Português

### Troféus do PS3
- Adicionada a aba **Troféus**, com os 12 troféus originais do jogo, imagens, nomes, descrições, categorias e contador de progresso. Filtros: Todos, Desbloqueados e Bloqueados.
- Opções independentes para registrar troféus e mostrar notificações. Conquistas são registradas localmente pelos eventos do jogo; não há sincronização com a PSN nem desbloqueio retroativo com base em saves antigos.
- Novo cartão de conquista com imagem, categoria, nome e descrição, som, animação de entrada/saída e cinco segundos de exibição. Desbloqueios simultâneos entram em fila.
- Posição configurável nos quatro cantos, aplicada durante o jogo e salva nas preferências. **Superior direito é o padrão.**
- Novo ícone de taça. Redefinir troféus pede confirmação, guarda backup e fica indisponível enquanto o jogo está rodando.
- Interface e textos dos troféus nos 13 idiomas do launcher. As imagens são lidas do pacote original do próprio jogo.

### Launcher e dados do jogador
- Seção **Mapas e saves** reorganizada, com ativação/desativação e instruções dos atalhos de teclado e controles para escolher mapas, salvar em qualquer lugar e carregar saves.
- Adicionada rolagem vertical às notas de atualização, com roda do mouse, barra arrastável e teclado, permitindo ler o texto completo.
- Reforçada a proteção de dados pessoais no atualizador: troféus, saves nativos, marcadores F7 e arquivos `.cvsav` não são substituídos, inclusive em instalações organizadas com `bin/`.
- Mantidas as correções anteriores de iluminação/tela branca, áudio, filtro de granulado, FSR Frame Generation e relatório leve de suporte. Esta versão não inclui os experimentos pesados de diagnóstico ou sincronização do FSR.

**Downloads separados:** `CodeVeronica-PC-v1.1.6.zip` e `Veronica-PKG-Extractor-v1.0.1.zip`. O jogo, firmware, licenças RAP, saves e configurações pessoais não acompanham o port.

## English

### PS3 trophies
- Added a **Trophies** page showing all 12 original trophies, original icons, localized names/descriptions, grades, progress, and All/Unlocked/Locked filters.
- Separate switches for trophy recording and notifications. Unlocks are recorded locally from the game's trophy events; there is no PSN synchronization or retroactive save scanning.
- Compact achievement card with icon, name, description, grade, sound, slide/fade animations and a five-second hold. Multiple unlocks are queued.
- Notification placement can be changed live and saved in any of the four screen corners. **Top right is the default.**
- Redesigned cup icon. Reset requires confirmation, keeps a backup and is unavailable while the game is running. Trophy reset does not modify game saves.
- Trophy UI and descriptions support all 13 launcher languages. Icons are loaded from the player's original game package, not distributed with the port.

### Launcher and player data
- Redesigned **Maps and saves** section with a toggle and keyboard/controller shortcuts explaining map selection, saving anywhere and loading saves.
- Release notes now scroll vertically with the mouse wheel, draggable scrollbar and keyboard, so longer update notes remain readable.
- Strengthened updater exclusions for trophy progress, native saves, F7 markers and `.cvsav` files, including organized `bin/` layouts.
- Retains previous lighting/white-flash, audio, film-grain, FSR Frame Generation and lightweight support-log fixes. Heavy diagnostic/profiling and experimental FSR synchronization paths are not included.

**Separate downloads:** `CodeVeronica-PC-v1.1.6.zip` and `Veronica-PKG-Extractor-v1.0.1.zip`. Game files, firmware, RAP licenses, saves and personal settings are not included.

## Technical notes / Notas técnicas

- The host trophy catalog reads bounded TRP archives and XML with DTDs disabled. The known CVX HD catalog contains 9 bronze, 2 silver and 1 gold trophy, without a platinum trophy.
- Corrected `sceNpTrophy` SDK structure sizes, error codes, big-endian numeric fields and CellRtcTick output timestamps. Preserved installed/deferred registration callbacks. Actual unlock events feed the local host backend.
- Progress uses per-path writer serialization, merge-before-write, flushed temporary files and atomic replacement. Corrupt progress is not silently overwritten; legacy HLE filenames are imported only when no canonical file exists.
- Notifications rasterize once per event. Each already-fenced frame slot owns its texture, upload buffer and descriptor. Animation updates six vertices and opacity; no new GPU queue drain or per-frame image upload was added. Notification sound is embedded and starts asynchronously.
- No FPS improvement or zero-cost claim is made. Validation includes trophy guest-memory/ABI, persistence/reset/backup/corrupt-data tests across EU/US/JP packages, all 13 launcher languages, notification timing/queue/position tests, GPU texture tests, and updater preservation fixtures. The first trophy has also been confirmed working in the user's gameplay test.
- Prior FSR edge-artifact improvement remains hardware-dependent; this release does not claim NVIDIA-wide visual confirmation.
