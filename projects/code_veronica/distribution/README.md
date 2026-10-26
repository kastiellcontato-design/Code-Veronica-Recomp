# Build de distribuição / Distribution build

Os arquivos desta pasta preparam a distribuição Windows sem console e sem as
capturas técnicas F12/F9. F9 continua disponível para mods; F10 tira fotos.
Não muda as opções gráficas salvas nem inclui configurações do desenvolvedor.
Tracy e os experimentos de desenvolvimento ficam desativados.

## Compilar e empacotar

Use o ambiente de desenvolvimento x64 do Visual Studio, com CMake, Ninja,
clang-cl e Python 3.11+ disponíveis. A partir de projects/code_veronica:

```
python distribution/build.py
python tools/empacotar_release.py --distribution --output-dir build-distribution/pacote
```

A compilação reutiliza os objetos recompilados de PPU/SPU já existentes nas
três builds regionais após conferir as fontes geradas. O código host, launcher
e runtime são compilados na pasta separada build-distribution. Os executáveis
normais em build-release/bin e o INI local são preservados e conferidos por hash.
O empacotador valida o modo Windows GUI e os hashes do resultado da build.
Não sobrescreve um ZIP anterior. Use outra pasta para uma nova tentativa.

O ZIP oferece somente Code Veronica.exe como entrada na raiz. Os três EXEs
regionais e as DLLs ficam em bin/, mantendo os nomes usados pelo LiveSplit.
Configurações, saves, mods e caches também ficam em bin/, como os caminhos
relativos do runtime. O launcher público usa CRT estático e não abre console.
O pacote inclui licenças, LEIA-ME.txt, README.txt e
release-manifest.json. Não inclui jogo, PKG, RAP, firmware, saves, INI pessoal,
CMDs, testes, perfiladores, PDBs ou caches. O Visual C++ x64 é um requisito;
os guias incluem o link oficial para instalação. Não há publicação automática.

Distribution executables use the Windows GUI subsystem and disable developer
capture hotkeys. Player shortcuts and graphics options remain available.
The package includes Portuguese and English player guides, required DLLs and
license notices. Game data, personal settings and saves are never packaged.
The commands above build and package locally; they do not publish a release.

## Aparências portáteis

cv_appearance_prepare.cpp gera os overlays de Wesker e dos 28 humanoides
diretamente dos arquivos do jogo selecionado. Apenas código e metadados de
conversão são distribuídos. A geração é nativa e por seleção, com zlib 1.3.2
estático; não depende de Python, arquivos ARC preparados nem caminhos locais.
O cache fica em appearance_cache/v1/<instalação>/<idioma>, verifica os arquivos
de origem, grava por troca atômica e refaz arquivos incompletos.
O atualizador protege essa pasta. Nunca inclua seu conteúdo num pacote público.

O alvo code_veronica_appearance_prepare_tests verifica os 33 modelos/prévias,
reuso/reparação de cache e isolamento de um personagem ausente. Recebe a pasta
system do jogo e uma pasta temporária gravável. Não executa o jogo.

## Downloads separados em cada release estavel

Anexar `Veronica-PKG-Extractor-v1.0.zip` como asset independente em toda nova release estavel, mesmo que o extrator nao tenha mudado de versao. Ele deve aparecer ao lado do ZIP principal nos Assets do GitHub; nunca dentro do ZIP do launcher. Reutilizar o pacote oficial ja publicado em v1.0 e conferir seu SHA-256 antes de anexar.

Pacote oficial atual: SHA-256 `376922e267402094c00950a972e32339d6839cea480f5f15bd909fae615f5812`.

Antes de concluir a publicacao, verificar que a release contem somente os ZIPs necessarios: o ZIP principal e o ZIP separado do Veronica Extractor. Manter o arquivo .sha256 apenas na verificacao local, sem anexa-lo aos Assets publicos, conforme preferencia do mantenedor. Se o extrator ganhar uma nova versao, atualizar esta referencia para o novo pacote validado.
