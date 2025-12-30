## vm - text pager

### installation:

* clone the repo:
   git clone https://github.com/txcase/vm.git

* run the build script:
   - make the script executable:
     chmod +x ./bl
   - run it:
     ./bl

### note:
the executable file is not installed into any directory listed in $path.

### usage: vm -[hHv] file
*   -v     print version*
*   -h/H   print usage*

### interactive mode usage:
- '^' = CTRL
- q     quit
- j     y position += 1
- k     y position -= 1
- ^D    y position += win_height
- ^U    y position -= win_height
- g     go to last line
- G     go to first line
- h     help message
- /     search (the search is not case sensitive)
-   n     next
- tt    to N line

version: 0.05
