/* -*- C++ -*- */
#ifndef FTextFilter_h
#define FTextFilter_h

/* FTextFilter.h, Copyright (c) 2003, Scion Studios
 * ===========================================================================
 *
 *  Program: FTextFilter.h
 *  Author : Brian C. Ladd
 *  Purpose: Interface for FTextFilter, a flexible class of composable text
 *  manipulation filters.
 *  Created: Fri Sep 26 08:39:20 2003 by blad
 *  Revised: Mon Sep 29 11:30:20 2003 by blad
 *
 * ===========================================================================
 */

#include "EditorPrivate.h"
class CharacterSource 
{

public:
  CharacterSource() {}

  FString CurrentLocation() {
	  if (!providerStack_.Num()) return FString();
	  return providerStack_.Top()->CurrentLocation();
  }

  // Push a new source atop the stack of character providers
  void Add(const TCHAR * b, const TCHAR * e) {
    providerStack_.Push(NEW CharacterProvider(b, e));
  } // Add 
  
  // Push a new file source on the stack of character providers; note that the file contents must be
  // read into a buffer before calling this routine.
  void AddFile(const TCHAR * b, const TCHAR * e, const FString & f, INT l=0) {
	  providerStack_.Push(NEW CharacterProvider(b, e, f, l));
  } // AddFile

  void AddMacro(const TCHAR * b, const TCHAR * e, const FString & m, TArray<FString> * parameters) {
	  providerStack_.Push(NEW CharacterProvider(b, e));
	  providerStack_.Top()->macroName(m);
	  providerStack_.Top()->AddParameters(parameters);
  }

  TCHAR peekChar() const {
    if (!providerStack_.Num()) return '\0';
    if (!(*providerStack_.Top())) return '\0';
    
    return providerStack_.Top()->peekChar();
  } // peekChar

  TCHAR getChar() {
	  UBOOL fileChanged = false;
	  // while there are elements on the stack AND they are out
	  // of characters, pop them off and dispose of them
	  while ((providerStack_.Num() != 0) && !(*providerStack_.Top())) {
		  CharacterProvider * empty = providerStack_.Pop();
		  fileChanged = fileChanged || (empty->fileName() != TEXT(""));
		  delete empty;
	  }
	if (!UBOOL(*this)) return '\0';
	if (fileChanged) {
		providerStack_.Top()->pushString(FString(TEXT("#linenumber ")) + FString(appItoa(providerStack_.Top()->lineNumber())) + FString(TEXT("\n")));
	}

    return providerStack_.Top()->getChar();
  } // getChar

  void ungetChar(TCHAR ch) {
    if (providerStack_.Num())
      providerStack_.Top()->ungetChar(ch);
  } // ungetChar

  void pushString( const FString & str ) {
    if (providerStack_.Num())
      providerStack_.Top()->pushString(str);
  } // pushString

  operator UBOOL() {
        // only thing that could be left on the stack is a CharacterProvider
        // that is not empty. Thus if the stack is empty, we are done (false)
        // and if the stack is not empty, input remains.
    return (providerStack_.Num() != 0); 
    
  }
  
  UBOOL lookup(const FString & macroName, FString & definition) {
	  if (providerStack_.Num() == 0)
		  return false;
	  return providerStack_.Top()->lookup(macroName, definition);
  }

private:
  /***************************************************************************\
   @ CharacterProvider
   ---------------------------------------------------------------------------
   description  : A source for characters. Contstructed with a beginning and
                  ending pointer at a buffer of characters. Also supports
                  arbitrary ungetChar/PutBackString operations that insert
                  characters at the front of the remaining sequence. 
  \***************************************************************************/
  class CharacterProvider
  {
  public:
    CharacterProvider(const TCHAR * begin, const TCHAR * end, 
                      const FString &file = FString(), INT lineNo = 0)
        : copyOfInput_(begin),  fileName_(file), lineNumber_(lineNo), 
		  charNumber_(0), symbolTablePtr_(0), macroFinished_(false) {
			  begin_ = *copyOfInput_;
			  end_ = begin_ + (end - begin);
		  }
    
        // return a copy of the next character without consuming it
    TCHAR peekChar() const {
      INT dLen = diversionStack_.Len();
      if (dLen)
        return diversionStack_[dLen - 1];
      else
        return *begin_;
    } // peekChar
    
      // return and consume the next character in the input
    TCHAR getChar() {
      TCHAR retval;
      INT dLen = diversionStack_.Len();
      if (dLen) {
        retval = diversionStack_[dLen - 1];
		diversionStack_=diversionStack_.Left(dLen-1);
      } else  if (begin_ != end_) {
        retval = *begin_;
        ++begin_;
	  } else {
		  retval = '\0';
		  macroFinished_ = true;
	  }

	  ++charNumber_;
	  if ((retval == 0x0A) ||
		  ((retval == 0x0D) && (peekChar() != 0x0A)))
		  ++lineNumber_;
      
	  return retval;
    } // getChar
    
      // push a single character back on the top of the diversion stack
    void ungetChar(TCHAR ch) {
	  if (ch == 0x00) return;

	  if ((ch == 0x0D) || (ch == 0x0A))
		  --lineNumber_;
	  --charNumber_;
      diversionStack_ += ch;
    } // ungetChar
    
      // push all the elements onto the diversion stack; requires reversing the incoming
      // string first so that the characters pop off in the right order
    void pushString(const FString & str) {
      diversionStack_ += str.Reverse();
    } // pushString
    
    operator UBOOL() const {
      return (diversionStack_.Len() || (begin_ != end_)) || !macroFinished_;
    }
    
	FString macroName() const {
		return macroName_;
	}
	FString macroName(const FString & mN) {
		macroName_ = mN;
		return macroName_;
	}
	FString fileName() {
		return fileName_;
	}
	FString fileName(const FString & fN) {
		fileName_ = fN;
		return fileName_;
	}
	INT lineNumber() {
		return lineNumber_;
	}
	FString CurrentLocation() {
		if (macroName_ != TEXT("")) {
			return FString::Printf(TEXT("(%s):%d"), *macroName_, lineNumber_+1);
		} else if (fileName_ != TEXT("")) {
			return FString::Printf(TEXT("%s:%d"), *fileName_, lineNumber_+1);
		} else {
			return FString::Printf(TEXT(""));
		}
	} // CurrentLocation

	void AddParameters(TArray<FString> * parameters) {
		if (parameters != NULL) {
			symbolTablePtr_ = NEW TMap<FString, FString>;
			FString ndx = appItoa(0);
			symbolTablePtr_->Add(*ndx, *macroName_);
			for (INT i = 0; i < parameters->Num(); ++i) {
				ndx = appItoa(i+1);
				symbolTablePtr_->Add(*ndx, *(*parameters)(i));
			}
			ndx = TEXT("#");
			FString count = appItoa(parameters->Num());
			symbolTablePtr_->Add(*ndx, *count);
		}
	} // AddParameters

	UBOOL lookup(const FString & macroName, FString & definition) {
		if ((symbolTablePtr_ == NULL) || (symbolTablePtr_->Find(macroName) == NULL))
			return false;
		definition = *symbolTablePtr_->Find(macroName);
		return true;
	}

  private:
	TMap<FString, FString> * symbolTablePtr_;
        // name of the macro being expanded in this CharacterProvider
    FString macroName_;
        // the name of the file being expanded in this CharacterProvider
    FString fileName_;
        // Line number inside this CharacterProvider
    INT lineNumber_;
    INT charNumber_;
	FString copyOfInput_;
    const TCHAR * begin_;
    const TCHAR * end_;

	UBOOL macroFinished_;
        // a stack of characters (all insertions are reversed so insertion/deletion takes place at the
        // END of the diversionStack.
    FString diversionStack_;
  }; /** End of CharacterProvider **/

  TArray<CharacterProvider *> providerStack_;
  
  
}; /** End of CharacterSource **/
/*****************************************************************************\
 @ FTextFilter
 -----------------------------------------------------------------------------
 description  : The base class for text manipulation filters.
 notes        : 
\*****************************************************************************/
class FTextFilter
{
public:
  FTextFilter(const FString & fileName, FFeedbackContext* Warn = GWarn)
	  : fileName_(fileName), Warn_(Warn) { }

      /* Compiler generated copy constructor and assignment operator are
         fine for the general case. */

  virtual ~FTextFilter() {}

      /* The meat of the filter. Filters are provided with pointers to the beginning and
	     end of a character buffer. They return a result buffer built by "processing"
		 the contents of the input buffer. Processing has different meaning for different
		 filters. 
	 */
  virtual void Process(const TCHAR * Begin, const TCHAR * End, FString &Result) = 0;
protected:
  FString fileName_;
    // Where do warnings get sent?
  FFeedbackContext * Warn_;
}; /** End of FTextFilter **/

/*****************************************************************************\
 @ SequencedTextFilter
 -----------------------------------------------------------------------------
 description  : Composes two text filters, returning the result of applying
                the second filter to the result of the first text filter.
 notes        : 
\*****************************************************************************/
class SequencedTextFilter : public FTextFilter
{
public:
  SequencedTextFilter(FTextFilter & first, FTextFilter & second, const FString & fileName, FFeedbackContext* Warn = GWarn)
      : first_(first), second_(second), FTextFilter(fileName, Warn) {}

  virtual void Process(const TCHAR * Begin, const TCHAR * End, FString &Result) {
    FString intermediate;
    const TCHAR * intermediateBegin;
	const TCHAR * intermediateEnd;

    first_.Process(Begin, End, intermediate);
    
    intermediateBegin = *intermediate;
    intermediateEnd = intermediateBegin + intermediate.Len();
    second_.Process(intermediateBegin, intermediateEnd, Result);
  } // Process
   
  
private:
  FTextFilter & first_;
  FTextFilter & second_;
  
}; /** End of SequencedTextFilter **/

/*****************************************************************************\
 @ CommentStrippingFilter
 -----------------------------------------------------------------------------
 description  : Given a buffer of text, replaces all //-style comments with an 
                end of line character sequence (stripping the comment) and replacing
				all /* and * / (you know why the space is there) comments with
				a single space if they are on a single line or replacing them with 
				the appropriate number of new lines if they span multiple lines. 
 notes        : Quotes are respected outside of comments (so "//" doesn't begin a
                comment).
\*****************************************************************************/
class CommentStrippingFilter : public FTextFilter
{
public:
  CommentStrippingFilter(const FString & fileName, FFeedbackContext* Warn = GWarn) 
	  : FTextFilter(fileName, Warn) {}

  virtual void Process(const TCHAR * Begin, const TCHAR * End, FString &Result);
private:
  CharacterSource inBuffer_;
}; /** End of CommentStrippingFilter **/

/*****************************************************************************\
 @ MacroProcessingFilter
 -----------------------------------------------------------------------------
 description  : Processes a buffer of text, expanding macro expressions. Constructed
                with the name of the package; this gives an initial search path for
				included files.
 notes        :
\*****************************************************************************/
class MacroProcessingFilter : public FTextFilter
{
public:
	MacroProcessingFilter(const TCHAR * pName, const FString & fileName, FFeedbackContext* Warn = GWarn);

	virtual void Process(const TCHAR * Begin, const TCHAR * End, FString &Result);
	// ----- the character that begins a macro
	static const TCHAR macroInvitationChar = '`';
	static const TCHAR macroBeginNoExpand = '(';
	static const TCHAR macroEndNoExpand = ')';
	static const TCHAR macroBeginNameWrap = '{';
	static const TCHAR macroEndNameWrap = '}';
	
private:
	UBOOL lookup(const FString & macroName, FString & definition) {
		if (inBuffer_.lookup(macroName, definition))
			return true;

		if (symbolTable_.Find(macroName) == NULL)
			return false;

		definition = *symbolTable_.Find(macroName);
		return true;
	} // lookup
	void initSymbolTable();
	FString getName();
	FString getUnexpanded();
	FString getParam();
	INT getParamList(TArray<FString> & Params);
	void processIncludeFile(const FString & fileName);
	FString expand(const FString & macroName);
	    // should this macro name be expanded even if text is not being expanded;
	    // used to catch else and endif
	UBOOL isHandledWhenNotEmitted(const FString & macroName);

	    // what package are we part of? Important for relative include file lookup
	FString packageName_;
	    // number of nested if/endif pairs processing is inside of. Permits nesting
	INT nestedIfCounter_;
	    // flag to support conditional compilation (macros can turn emitted characters on and off)
	TArray<UBOOL> emitCharacters_;
	    // the provider of characters: a stack of files, macros, etc. Keeps track of where the next char comes from
	CharacterSource inBuffer_;
	    // Global symbol table; there is only one except for macro parameters; those are kept in inBuffer_
	TMap<FString, FString> symbolTable_;
}; /** End of MacroProcessingFilter **/

#endif  /* FTextFilter_h */

