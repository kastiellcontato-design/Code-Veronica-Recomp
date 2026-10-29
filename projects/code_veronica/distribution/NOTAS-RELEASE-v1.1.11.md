# v1.1.11 — HD texture pack, faster launcher and texture switch fix

## English

- **HD texture pack (optional, separate download):** the texture mods accept HD and 4K images (PNG or DDS, any resolution), and a ready-made HD pack is now available for the **US, EU and JP** versions: **[download on Google Drive](https://drive.google.com/file/d/1t6yVG1yEC5dl3Td6jT4rhFj1EEwmbsAZ/view?usp=sharing)**. The textures are the work of other people in the Resident Evil community: **ewgeha** (main 4K pack), **SonicB00M** (fixes and English translation addon) and **JuanchoTexHD** (menus, interface and fonts), plus **BROSKO**, **Asan Salimov** and **Crazy Ivana**. The images were originally made for the PS2 version; the matching to the PS3 textures, the transparency fixes, the English text and the packaging were adapted for this project (Resident Evil Code: Veronica X PS3 HD Recomp PC) by **Kastiel**, with some modifications of his own; 46 door, plant, title screen and typewriter textures that no pack covered were redone with AI tools. All credits, channels and links, with the install steps, are in [TEXTURAS-HD-COMUNIDADE.md](https://github.com/kastieloficial/RE-Code-Veronica-PS3-HD-Recomp-PC/blob/code-veronica/projects/code_veronica/TEXTURAS-HD-COMUNIDADE.md). The pack is **not** part of the port's ZIP. It was built against the US version; EU and JP were checked by texture names, not played.
- **Faster launcher:** turning options on or off and switching tabs no longer waits on the settings file being rewritten key by key (about 140 ms per click before, about 12 ms now), and the Mods tab no longer recounts a large texture folder on every visit.
- **The launcher stays available after the game:** closing the game window brings the launcher back, with your settings, so you can change them and play again. Closing the launcher itself while the game runs still quits everything.
- **Texture mods switch (F9) fixed:** turning the mod off now returns to the original textures at once, and turning it on again brings the mod textures back, without restarting the game. This also works when the game text is set to Brazilian Portuguese, which used to keep already-loaded mod textures on screen.
- **Modern movement description:** the in-game option now says the gameplay is similar to Resident Evil HD Remaster and Resident Evil 0 HD Remaster. The Recomp Settings menu inside the game now follows the game text: Portuguese with the PT-BR translation on, English otherwise (also on the Japanese version), instead of the launcher language.
- **Portuguese translation:** the incendiary grenade name is corrected in this package.
- Previous improvements are retained: optional modern controls, remappable analog sticks, combat HUD, quick equipment, manual reload and shader compiler selection.

EU/US/JP builds compiled successfully and were tested locally in gameplay on the US version; the EU and JP builds were not played. This remains a work in progress; a full playthrough on every setup is not verified.

### Downloads

- **CodeVeronica-PC-v1.1.11.zip** — main Windows port. Extract the complete ZIP and open **Code Veronica.exe**, keeping `bin/` and `assets/` beside it. SHA-256: `a9c3ec5567ec84628648d689ba08da9aa4db4866b484abce34b532fd6da7fe05`.
- **Veronica-PKG-Extractor-v1.0.1.zip** — PKG preparation tool, a **separate** ZIP (latest version, unchanged). The port needs your game's extracted PKG, and this tool prepares it.
- **HD texture pack** — optional, [Google Drive](https://drive.google.com/file/d/1t6yVG1yEC5dl3Td6jT4rhFj1EEwmbsAZ/view?usp=sharing) (not attached to this release).

No original game files, game 3D models, saves or personal settings are bundled. Supply your own original PS3 game.

## Português

- **Pacote de texturas HD (opcional, download separado):** os mods de textura aceitam imagens HD e 4K (PNG ou DDS, qualquer resolução), e agora há um pacote HD pronto para as versões **americana, europeia e japonesa**: **[baixar no Google Drive](https://drive.google.com/file/d/1t6yVG1yEC5dl3Td6jT4rhFj1EEwmbsAZ/view?usp=sharing)**. As texturas são trabalho de outras pessoas da comunidade de Resident Evil: **ewgeha** (pacote principal em 4K), **SonicB00M** (correções e tradução para o inglês) e **JuanchoTexHD** (menus, interface e fontes), além de **BROSKO**, **Asan Salimov** e **Crazy Ivana**. As imagens foram feitas originalmente para a versão de PS2; a ligação com as texturas do PS3, a correção de transparência, o texto em inglês e o empacotamento foram adaptados para este projeto (Resident Evil Code: Veronica X PS3 HD Recomp PC) por **Kastiel**, com algumas modificações dele; 46 texturas de portas, plantas, tela de título e máquina de escrever que nenhum pacote cobria foram refeitas com ferramentas de IA. Todos os créditos, canais e links, com as instruções de instalação, estão em [TEXTURAS-HD-COMUNIDADE.md](https://github.com/kastieloficial/RE-Code-Veronica-PS3-HD-Recomp-PC/blob/code-veronica/projects/code_veronica/TEXTURAS-HD-COMUNIDADE.md). O pacote **não** faz parte do ZIP do port. Foi montado em cima da versão americana; a europeia e a japonesa foram conferidas pelos nomes das texturas, sem jogar.
- **Launcher mais rápido:** ligar e desligar opções e trocar de aba não espera mais o arquivo de configurações ser reescrito chave por chave (cerca de 140 ms por clique antes, cerca de 12 ms agora), e a aba Mods não conta de novo uma pasta grande de texturas a cada visita.
- **O launcher continua disponível depois do jogo:** ao fechar a janela do jogo, o launcher volta, com as suas configurações, para você mudar e jogar de novo. Fechar o próprio launcher durante o jogo ainda fecha tudo.
- **Liga/desliga das texturas (F9) corrigido:** ao desligar, as texturas voltam ao original na hora, e ao ligar de novo as do mod voltam, sem reiniciar o jogo. Também funciona com o texto do jogo em português do Brasil, que antes mantinha na tela as texturas do mod já carregadas.
- **Descrição do movimento moderno:** a opção dentro do jogo agora diz que a jogabilidade é parecida com a de Resident Evil HD Remaster e Resident Evil 0 HD Remaster. O menu Recomp Settings dentro do jogo agora segue o texto do jogo: português com a tradução PT-BR ligada e inglês nos demais casos (também na versão japonesa), e não mais o idioma do launcher.
- **Tradução em português:** o nome da granada incendiária está corrigido neste pacote.
- Mantidas as melhorias anteriores: controle moderno opcional, analógicos remapeáveis, HUD de combate, equipamento rápido, recarga manual e seleção do compilador de shaders.

As builds EU/US/JP compilaram com sucesso e foram testadas localmente em jogo na versão americana; as builds EU e JP não foram jogadas. O projeto continua em desenvolvimento; uma campanha completa em cada configuração não foi verificada.

### Downloads

- **CodeVeronica-PC-v1.1.11.zip:** port principal. Extraia tudo e abra **Code Veronica.exe**, mantendo `bin/` e `assets/` ao lado. SHA-256: `a9c3ec5567ec84628648d689ba08da9aa4db4866b484abce34b532fd6da7fe05`.
- **Veronica-PKG-Extractor-v1.0.1.zip:** ferramenta para preparar o PKG, em **ZIP separado** (última versão, sem alterações). O port precisa do PKG extraído do seu jogo, e esta ferramenta o prepara.
- **Pacote de texturas HD:** opcional, no [Google Drive](https://drive.google.com/file/d/1t6yVG1yEC5dl3Td6jT4rhFj1EEwmbsAZ/view?usp=sharing) (não anexado a esta release).

Não inclui arquivos originais do jogo, modelos 3D do jogo, saves nem configurações pessoais. É necessário fornecer sua própria cópia original do jogo de PS3.
