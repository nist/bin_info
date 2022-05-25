#include <bfd.h>
#include <loader.h>

/* FUNCTION: open_bfd
 * INPUT ARGUMENTS:
 * 	fname : name of the binary file to open
 * PROCESS:
 * 	a) open file
 * 	b) check if the file is indeed a binary executable file
 * 	c) check the file format (ELF, PE, UNKNOWN)
 * RETURN VALUE:
 * 	static bfd * : pointer to bfd structure defining the binary
 */
static bfd *
open_bfd(std :: string &fname) {
	bfd		*bfd_h;			/* pointer to binary executable file */
	static int	bfd_init_flag = 0;

	if ( !bfd_init_flag ) {
		bfd_init();		/* initialize internal data structures of libbfd */
		bfd_init_flag = 1;	/* set flag appropriatly */
	}

	if ( !( bfd_h = bfd_openr(fname.c_str(), NULL) ) ) {
		fprinitf(stderr, "[!!] Failed to open binary '%s' (%s)\n",
			 fname.c_str(), bfd_errmsg(bfd_get_error()));
		return NULL;
	}

	/* bfd_object: describes executable, relocatable object or shared library */
	if ( !bfd_check_format(bfd_h, bfd_object) ) {
		fprintf(stderr, "[!!] File '%s' does not appear to be an executable (%s)\n",
			fname.c_str(), bfd_errmsg(bfd_get_error()));
		return NULL;
	}

	/* some versions of bfd_check_format set a wrong format error before detecting 
	 * format and then neglect to unset it once format has been detected, we unset
	 * it manually to prevent issues.
	 */
	bfd_set_error(bfd_error_no_error);

	/* bfd flavours: binary format (elf, coff, msdos, mach_o, etc. ) */
	if ( bfd_get_flavour(bfd_h) == bfd_target_unkown_flavour ) {
		fprintf(stderr, "[!!] Unrecognized format for binary '%s' (%s)\n",
			fname.c_str(), bfd_errmsg(bfd_get_error()));
		return NULL;
	}

	return bfd_h;
}
/* FUNCTION: load_symbols_bfd
 * INPUT ARGUMENTS:
 * 	bfd_h	: binary's bfd header
 * 	bin	: binary object
 * PROCESS:
 * 	a) read size of symbol table in binary file
 * 	b) allocate heap space to store symbol table entries
 * 	c) read symbol table
 * 	d) filter out symbols associated with functions
 * 	e) return
 * RETURN VALUE:
 * 	static int : status code
 * 		 0 - success
 * 		-1 - failure 
 */
static int
load_symbols_bfd(bfd *bfd_h, Binary *bin) {
	int	ret;
	long	n, nsyms, i;		/* n: total size of symbol table (in bytes)
					 * nsysm: number of symbols in binary
					 * i: loop iterator
					 */
	asymobl	**bfd_symtab;		/* symbol table */
	Symbol	*sym;			/* single symbol instance */

	bfd_symtab = NULL;

	/* get size of symbol table */
	if ( ( n = bfd_get_symtab_upper_bound(bfd_h) ) < 0 ) {
		fprintf(stderr, "[!!] Failed to read symtab (%s)\n",
			bfd_errmsg(bfd_get_error()));
		goto fail;
	} else if ( n ) {
		/* allocate memory for symbol table */
		if ( !( bfd_symtab = (asymbol **) malloc(n) ) ) {
			fprintf(stderr, "[!!] Out of memory\n");
			goto fail;
		}

		/* read symbols from binary */
		if ( ( nsyms = bfd_canonicalize_symtab(bfd_h, bfd_symtab) ) < 0 ) {
			fprintf(stderr, "[!!] Failed to read symbols table (%s)\n",
				bfd_errmsg(bfd_get_error()));
			goto fail;
		}

		/* filter the symbols relating to functions */
		for ( i = 0; i < nsyms; ++i ) {
			if ( bfd_symtab[i] -> flag & BSF_FUNCTION ) {
				bin -> symbols.push_back(Symbol());
				sym = &bin -> symbols.back();
	
				sym -> type = Symbol :: SYM_TYPE_FUN;
				sym -> name = std :: string(bfd_symtab[i] -> name);
				sym -> addr = bfd_asymbol_value(bfd_symtab[i]);
			}
		}
	}

	ret = 0;
	goto cleanup;

	fail:
		ret = -1;
	cleanup:
		if ( bfd_symtab )
			free(bfd_symtab);

	return ret;
}

/* FUNCTION: load_binary_bfd
 * INPUT ARGUMENTS:
 * 	fname	: name of binary file to load
 * 	bin	: structure to hold information of binary file
 *	type	: enum to set value in bin for type of the binary file
 * PROCESS:
 * 	a) set filename and entry point in bin
 * 	b) set executalbe type in bin
 * 	c) set target architecture type in bin
 * 	d) load static symbols (if present)
 * 	e) load dynamic symbols
 * 	f) load sections
 * 	g) cleanup
 * RETURN VALUE:
 * 	static int : status code
 * 		 0 - success
 * 		-1 - failure 
 */
static int
load_binary_bfd(std :: string fname, Binary *bin, Binary :: BinaryType type) {
	int				ret;
	bfd				*bfd_h;
	const bfd_arch_info_type	*bfd_info;

	bfd_h	= NULL;
	bfd_h	= open_bfd(fname);

	if ( !bfd_h ) {
		goto fail;
	}

	/* setting general information */
	bin -> filename	= std :: string(name);			/* executable name */
	bin -> entry	= bfd_get_start_address(bfd_h);		/* executable entry point */

	/* setting appropriate executable type */
	bin -> type_str = std :: string(bfd_h -> xvec -> name);

	switch ( bfd -> xvec -> flavour ) {
		case bfd_target_elf_flavour:
			bin -> type = Binary :: BIN_TYPE_ELF;
			break;
		case bfd_target_coff_flavour:
			bin -> type = Binary :: BIN_TYPE_PE;
			break;
		case bfd_target_unknown_flavour:
		default:
			fprintf(stderr, "unsupported binary type (%s)\n", bfd_h -> xvec -> name);
			goto fail;
	}

	/* setting appropriate executable architecture */
	bfd_info	= bfd_get_arch_info(bfd_h);
	bfd -> arch_str = std :: string(bfd_info -> printable_name);

	switch ( bfd_info -> mach ) {
		case bfd_mach_i386_i386:
			bin -> arch = Binary :: ARCH_X86;
			bin -> bits = 32;
			break;
		case bfd_mach_x86_64:
			bin -> arch = Binary :: ARCH_X86;
			bin -> bits = 64;
			break;
		default:
			fprintf(stderr, "unsupported architecture (%s)\n", bfd_info -> printable_name);
		       goto fail;
	}

	/* symbols may not be present if the binary is stripped */
	load_symbols_bfd(bfd_h, bin);
	load_dynsym_bfd(bfd_h, bin);	/* TODO */

	if ( load_sections_bfd(bfd_h, bin)  /* TODO */ < 0 ) goto fail;

	ret = 0;
	goto cleanup;

	fail:
		ret = -1;
	
	cleanup:
		if ( bfd_h ) bfd_close(bfd_h);
	
	return  ret;
}

/* FUNCTION: load_binary
 * INPUT ARGUMENTS:
 * 	fname	: name of binary file to examine
 * 	bin	: structure to load information into
 * 	type	: supported types of binary file
 * PROCESS:
 * 	a) load the binary and examine
 * RETURN VALUE:
 * 	int : status code
 * 		 0 - success
 * 		-1 - failure 
 */
int
load_binary(std :: string &fname, Binary *bin, Binary :: BinaryType type) {
	return load_binary_bfd(fname, bin, type);
}

/* FUNCTION: unload_binary
 * INPUT ARGUMENTS: TBD
 * PROCESS: TBD
 * RETURN VALUE: TBD
 */
void
unload_binary(void) {
	/* TODO */
	return;
}
