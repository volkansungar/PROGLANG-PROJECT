# PROGLANG-PROJECT
Bilgisayar Mühendisliği 2. sınıf Programming Languages dersi projesi \
Prototype kodu FSM kullanılmamış deneme/öğrenme amaçlı test kodudur.
<h2>lexer.c error handling düzenlenecek</h2>

# ~/.bashrc
Usage: lexer <input_filename> <br />
`lexer() {
  echo "Compiling..."
  gcc lexer.c -o lexer
  if [ $? -eq 0 ]; then
    echo "Compilation successful. Running..."
    ./lexer "$1"
  else
    echo "Compilation failed."
  fi
}`
