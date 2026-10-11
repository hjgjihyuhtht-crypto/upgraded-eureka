# Gera um .c com o conteúdo binário de um arquivo: cmake -DIN=... -DOUT=... -DNAME=... -P embed_file.cmake
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" len)
math(EXPR size "${len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${hex}")
file(WRITE "${OUT}" "#include <stddef.h>\nconst unsigned char ${NAME}[] = {${body}};\nconst size_t ${NAME}_size = ${size};\n")
