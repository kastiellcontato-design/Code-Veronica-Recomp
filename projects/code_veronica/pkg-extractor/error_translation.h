#pragma once
#include <string>
inline std::wstring pkg_error_pt(const std::string& error){
 struct Row{const char* en;const wchar_t* pt;};
 static const Row rows[]={

{"Truncated package metadata.",L"Os metadados do pacote estão incompletos."},
{"Truncated PARAM.SFO.",L"O arquivo PARAM.SFO está incompleto."},
{"Absolute or empty package path.",L"O pacote contém um caminho absoluto ou vazio."},
{"Package filename is too long.",L"Um nome de arquivo no pacote é longo demais."},
{"AES stream length mismatch.",L"O tamanho dos dados criptografados é inconsistente."},
{"Unterminated SFO key.",L"Uma chave do arquivo PARAM.SFO está incompleta."},
{"Cancelled.",L"Cancelado."},

{"Windows encryption operation failed.",L"Falha na operação de criptografia do Windows."},
{"Invalid UTF-8 package filename.",L"O pacote contém um nome de arquivo com codificação UTF-8 inválida."},
{"Invalid package filename.",L"O pacote contém um nome de arquivo inválido."},
{"Cannot read PKG file.",L"Não foi possível ler o arquivo PKG."},
{"Encrypted PKG range is invalid.",L"O intervalo de dados criptografados do PKG é inválido."},
{"Invalid PARAM.SFO.",L"O arquivo PARAM.SFO é inválido."},
{"Invalid SFO table.",L"A tabela do arquivo PARAM.SFO é inválida."},
{"Invalid SFO key.",L"O arquivo PARAM.SFO contém uma chave inválida."},
{"Invalid SFO value.",L"O arquivo PARAM.SFO contém um valor inválido."},
{"PKG header is missing.",L"O cabeçalho do PKG está ausente."},
{"Invalid PKG data table.",L"A tabela de dados do PKG é inválida."},
{"Invalid PKG content ID.",L"O identificador de conteúdo do PKG é inválido."},
{"Unsupported PKG file entry type.",L"O PKG contém um tipo de arquivo não compatível."},
{"Invalid PKG filename range.",L"O intervalo do nome de arquivo no PKG é inválido."},
{"Invalid PKG file size or offset.",L"O tamanho ou a posição de um arquivo no PKG é inválido."},
{"PKG size overflow.",L"O tamanho declarado no PKG excede o limite permitido."},
{"PKG file conflicts with a directory.",L"Um arquivo do PKG tem o mesmo caminho de uma pasta."},

 {"Cannot open selected PKG.",L"Não foi possível abrir o PKG selecionado."},
 {"This file is not a PS3 PKG.",L"Este arquivo não é um PKG de PlayStation 3."},
 {"Only retail PS3 game PKGs are supported.",L"Use o PKG original da versão PSN de PS3. Pacotes de desenvolvimento e de outros consoles não são aceitos."},
 {"PKG size does not match its header; download or copy may be incomplete.",L"O tamanho do PKG não corresponde ao cabeçalho. O arquivo pode estar incompleto."},
 {"This PKG is not one of the three supported Code Veronica releases.",L"Este PKG não é uma das três versões compatíveis: NPEB00553, NPUB30467 ou NPJB00135."},
 {"Only the base PSN release version 01.00 is supported; this is not a compatible base game.",L"Somente o jogo PSN base, versão 01.00, é compatível. Não selecione um pacote de atualização."},
 {"Only the base PSN game is supported, not updates or add-ons.",L"Selecione o jogo PSN base. Atualizações e complementos não são aceitos."},
 {"PKG and PARAM.SFO title IDs do not match.",L"O código do jogo no PKG difere do PARAM.SFO."},
 {"PKG does not contain PARAM.SFO and USRDIR/EBOOT.BIN.",L"O pacote não contém a estrutura esperada: PARAM.SFO e USRDIR/EBOOT.BIN."},
 {"Choose an existing destination folder.",L"Escolha uma pasta de destino existente."},
 {"A folder for this version already exists at the destination. Choose another folder; existing files will not be overwritten.",L"Já existe uma pasta dessa versão no destino. Escolha outra pasta; os arquivos existentes não serão sobrescritos."},
 {"Insufficient free disk space at destination.",L"Não há espaço livre suficiente no destino."},
 {"Destination contains a link or junction; choose a regular folder.",L"O destino contém um link ou junção. Escolha uma pasta comum."},
 {"Cannot create temporary extraction folder.",L"Não foi possível criar a pasta temporária de extração. Verifique a permissão de escrita."},
 {"Cannot create extracted file.",L"Não foi possível criar um arquivo no destino. Verifique as permissões."},
 {"Disk write failed during extraction.",L"Falha ao gravar no disco durante a extração."},
 {"Cannot finish extracted file.",L"Não foi possível concluir a gravação de um arquivo."},
 {"Cannot finalize extraction; destination may already exist.",L"Não foi possível finalizar a extração. A pasta de destino pode já existir."},
 {"PKG is incomplete or truncated.",L"O PKG está incompleto ou truncado."},
 {"Duplicate or ambiguous filename in PKG.",L"O PKG contém nomes de arquivo duplicados ou ambíguos."},
 {"Unsafe package path component.",L"O PKG contém um caminho de arquivo inválido."},
 {"Reserved Windows package filename.",L"O PKG contém um nome de arquivo reservado pelo Windows."},
 {"Unsupported Windows package filename.",L"O PKG contém um nome de arquivo incompatível com o Windows."}
 };
 for(const auto& row:rows)if(error==row.en)return row.pt;
 return L"O pacote contém dados inválidos ou não foi possível lê-lo. Confira o arquivo e tente novamente.";
}
