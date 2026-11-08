root : 
    what external tools / libraries are used at each stages ?
    if we consider making some decisions dynamically does it involve llms or its just rules ? does it increase cost for querying ?
    can each stage made here can be used as micro service or tool? and llm can call whichever tool required ?
    does it lay foundation for multiple agents ?
    how other competitor tools like codex, github copilot, curie.. does this.
    which part is memory intensive and which part can be distributed ?
    what about error handling ? do we have something for that ?
    how do we test each stage and accuracy ?
    is it a rag ?
    why and how different is this from any RAG ?
    so why don't we embed codeElement using or our codePart using 137M embedding model why keep separate BM25 lexical terms and turn all into embeddings?

3) doesn't each language have its own grammer and it may be different from one another how we can make or map everything to common representation ?
how codeElement is generated what does it consider and what it doesn't 
    

4) tokeniser :
why only two types of tokens ? why not use other tokenisation schemes like BPE, word-gram or something similar ?
doesn't it create too much complexity and waster memory ?
which library or tool does we use ?
is it scalable ?
doesn't inverted index created something like mesh topology ?
6) Graph:
which tool / library is used..
do we create graph b/w codeElements ?
are Include, Call, Inherits from are codeElements..
why don't we use something like graphify 
how graph handles variables , fucntion, class ?
let say
main(){
int x,y;
a(); b();
return 0; 
}

a(){} b(){}

then how graph is created 

7) user query understanding:
why noise stripping is done ?
what if some important info is lost during noise stripping ?
who does this stage llm or our service?
why is it hardcoded ? how do we make it dynamic ?
why don't we use POS tagging or any other techniques aren't they useful ?
where does this stage exist like before parsing and all, before ranking or after tokinising or any place else and why is this there ?
why don't we use seq2seq RNN or some other thing to query processing ?
why don't we iterate and llm make more query undestanding or expansion..
where does embedding of this query is done ?

8) dual channel hybrid search & score fusion
why hardcode fusion parameter alpha = 0.8 ? 
how to make it dyanamic ?
why not give weight to other factors ?
where does ranking is done ?

9) bounded graph investigation
how 5 primary seeds are picked
what does snippet extraction do ? 

do we do here re ranking of whole list ?

for next stage do we consider whole sets of result (top-50 + 15 items) ?

10) Evidence sufficiency verification gate

what are rules ? are they hardcoded ? 
can we make it dynamic ?
does making it dynamic help ?

11) Context building & Prompt Engineering
how context builder is built ?
what does it expect and delivers ?
how does it handle multiple files code excerpts..
how does it fit everything under 4000 character budget ?

12) how we can make chat history and session context ? and how do we make it remember past conversations ?

