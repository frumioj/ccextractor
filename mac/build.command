g++ -Dfopen64=fopen -Dopen64=open -Dlseek64=lseek -I../src/gpacmp4 -o ccextractor $(find ../src/ -name '*.cpp') $(find ../src/ -name '*.c')
