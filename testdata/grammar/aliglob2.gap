input < raw, raw >

//type spair = ( string first, string second )

signature Alignment (alphabet, answer) {
  answer nil( <void, void>);
  answer del( < alphabet, void >, answer);
  answer ins( < void, alphabet >, answer);
  answer match( < alphabet, alphabet >, answer);
  choice [answer] h([answer]);
}


algebra count auto count;
algebra enum auto enum;
algebra tikz auto tikz;

grammar globsim uses Alignment (axiom=alignment) {
  tabulated{alignment}        
  alignment = nil( < EMPTY, EMPTY >)   |
              del( < CHAR, EMPTY >, alignment) |
              ins( < EMPTY, CHAR > , alignment ) |
              match( < CHAR, CHAR >, alignment) # h ;
}


instance enum = globsim(enum);

instance enumtikz = globsim ( enum * tikz ) ;
instance tikz = globsim ( tikz ) ;
