/******************************************************************\
*       BBC BASIC: no inline assembler (rosco-m68k, phase 1)       *
*       See architecture/Spec.md S5.4 (decision D3 open).          *
\******************************************************************/

void error (int, const char *) ;

// '[' : the assembler is not available on this platform
void assemble (void)
{
	error (255, "Sorry, not implemented") ;
}
