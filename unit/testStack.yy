query: 
        { $stack->push(); $stack->set("arg","LEFT"); } SELECT * from join { $stack->pop(undef) } |
        { $stack->push(); $stack->set("arg","RIGHT"); } SELECT * from join { $stack->pop(undef) } ;

join:
       { $stack->push() }      
       table_or_join 
       { $stack->set("left",$stack->get("result")); }
       { $stack->get("arg") } JOIN 
       table_or_join 
       ON 
       { $prng->arrayElement($stack->get("left")).".col = ".$prng->arrayElement($stack->get("result")).".col" }
       { $stack->pop([keys %{{map {$_=>1} (@{$stack->get("left")},@{$stack->get("result")})}}]) } ;

table_or_join:
        table | table | join ;

table:
       { $stack->push(); my $x = "t".$prng->digit();  $stack->pop([$x]); $x } ;
