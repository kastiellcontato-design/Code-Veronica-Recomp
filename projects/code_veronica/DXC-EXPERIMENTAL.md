# DXC experimental — 2026-10-16

O FXC continua sendo o compilador padrao. Os shaders e as opcoes de compilacao
do caminho FXC foram preservados. DXC e uma opcao de teste, escolhida antes de
abrir o jogo, e nao e salva nas configuracoes do launcher.

## Como testar e como voltar

- FXC: abra `executar-code-veronica-otimizado.cmd` normalmente. Esse iniciador
  escolhe FXC explicitamente, mesmo se o ambiente externo pedir DXC.
- DXC: abra `executar-code-veronica-dxc-experimental.cmd`.
- Para voltar: feche o jogo e abra o iniciador normal. Nao precisa recompilar,
  apagar cache, trocar executaveis ou remover DLLs.
- A escolha vale tambem quando o launcher inicia o executavel da outra regiao.
  Os executaveis europeu e japones desta entrega incluem o mesmo adaptador.

O log normal continua em `run-log-release.txt`; o experimental usa
`run-log-dxc-experimental.txt`. Procure `[SHADER] compiler:` para conferir o
compilador realmente escolhido. Em execucao direta do exe, a chave de teste e
`SHADER_COMPILER=dxc`; ausente ou diferente de `dxc` significa FXC.

## Limites conhecidos

Validacao do usuario em 2026-10-16: as versoes japonesa (NPJB00135) e europeia
(NPEB00553) apresentaram imagem correta nos trechos testados, com DXC confirmado
nos logs e sem erros de compilacao/fallback. Na europeia o usuario relatou grande
melhora percebida; ainda nao ha benchmark controlado, e essa execucao aproveitou
cache anterior. Isso nao certifica todos os cenarios nem resolve por comprovacao
as diferencas de sombras relatadas no experimento antigo.

O experimento anterior tinha sombras com listras/manchas. Esta integracao NAO
declara esse defeito resolvido. Usa operacoes `precise`, modo IEEE estrito e
HLSL 2016 somente no caminho DXC; isso exige comparacao visual com FXC nos mesmos
pontos, sobretudo sombras do isqueiro. Nao houve nova medicao que confirme o
ganho de 3x relatado no experimento anterior, nem promessa de eliminar travadas.

Nao sao executados os dois compiladores em cada draw. A opcao seleciona um
caminho por sessao; a compilacao de novos shaders continua nos workers existentes.

## DLLs e cache

- `dxcompiler.dll` e `dxil.dll` ficam ao lado de ambos os executaveis em
  `build-release/bin`. Foram copiadas juntas do Windows SDK 10.0.26100.0 x64
  instalado, versao de arquivo 1.8.2502.11, com assinaturas Microsoft validas.
- Nao ha dependencia estatica dessas DLLs. FXC nao precisa delas. DXC somente
  carrega a copia ao lado do executavel; nao procura em pastas aleatorias/PATH.
- Antes de selecionar DXC, o backend verifica Shader Model 6 e compila dois
  programas de verificacao. Se nao funcionar, a sessao inteira usa FXC e informa
  o motivo no log. Depois da selecao, falhas de shaders ficam no log: nao ha troca
  silenciosa de apenas um estagio para FXC, pois nao se pode misturar os formatos
  DXIL e DXBC nos estagios de um mesmo pipeline grafico.
- Erros visuais nao sao detectados automaticamente. Se aparecerem, volte pelo
  iniciador normal.
- Cache FXC: permanece em `shader_cache/`, com os mesmos nomes e chaves.
- Cache DXC: `shader_cache/dxc-exp1-<identidade das DLLs>/`, incluindo sua propria
  biblioteca `pipelines.bin`. A identidade usa tamanho/data das duas DLLs; o
  prefixo `exp1` identifica a politica de compilacao/ajustes de HLSL. Mudar essa
  politica exige mudar o prefixo no adaptador. Os arquivos FXC nao sao apagados.

## Manutencao

O adaptador esta em `libs/video/rsx_dxc.cpp/.h`. A divisao dos bancos de 16
texturas/samplers em declaracoes individuais ocorre so na copia de HLSL enviada
ao DXC. Isso permite remover recursos nao utilizados e respeitar a root signature
atual sem alterar os bindings do FXC. O backend seleciona o compilador uma vez,
com InitOnce; cada worker possui seu proprio objeto de compilador DXC.

Compilar sempre por `projects/code_veronica/build-release.cmd`.
As chaves opcionais de desenvolvimento, usadas somente ao compilar, sao:

- `CV_DXC_TESTS=1`: copia o par de DLLs do SDK selecionado por vcvars64, compila
  e executa os testes focados, incluindo criacao de pipeline DXIL no driver.
- `CV_FP_TESTS=1`: executa os testes existentes do tradutor de fragmentos.
- `CV_BUILD_JP=1`: pelo mesmo ponto de entrada, atualiza tambem o executavel JP
  usando o script regional existente. A compilacao normal continua sendo EU.

Os testes sao alvos EXCLUDE_FROM_ALL; nao entram na compilacao normal. As DLLs,
executaveis, caches, capturas e arquivos de jogo nao devem ser commitados.
