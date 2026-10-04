/* -*- C++ -*- */
/* FTextFilter.cpp, Copyright (c) 2003, Scion Studios
 * ===========================================================================
 *
 *  Program: FTextFilter.cpp
 *  Author : Brian C. Ladd
 *  Purpose: An abstract filter class for modifying a sequence of TCHAR
 *  Created: Fri Sep 26 08:37:05 2003 by blad
 *  Revised: Mon Sep 29 11:30:59 2003 by blad
 *
 *  Description:  Implementation for CommentStripping and MacroProcessing text
 *  filters.
 *
 * ===========================================================================
 */

#include "../Inc/FTextFilter.h"  // the interface for FTextFilter(s)

/* Process the buffer pointed to by Begin and End into Buffer. Buffer will contain 
   a copy of the text between Begin and End with all UnrealScript-style (C++-style)
   comments removed. Single line comments (// style) are replaced with the end of 
   line characters from the end of the line and multi-line comments (/* to * /) are
   replaced with a single space OR all internal end-of-line characters, whichever is
   longer. This keeps line counts the same in the input and output buffers
*/
void CommentStrippingFilter::Process(const TCHAR * Begin,
                                      const TCHAR * End,
                                      FString &Buffer)
{
  TCHAR ch;
  TCHAR matchQuote = ' ';
  UBOOL bQuoted = false, 
        bSingleLineComment = false, 
        bMultiLineComment = false,
        bWhitespaceGenerated = false, 
        bEscaped = false;
  
  inBuffer_.Add(Begin, End);

  while (inBuffer_) {
    ch = inBuffer_.getChar();
    
    if (bMultiLineComment) {
      if ((ch == TCHAR('*')) && inBuffer_ && (inBuffer_.peekChar() == TCHAR('/'))) {
        inBuffer_.getChar(); // consume second character (of 2 character sequence)
        if (!bWhitespaceGenerated)
          Buffer += TEXT(" "); // make sure a multiline comment generates _some_ white space
        bMultiLineComment = false;
      } else if ((ch == TCHAR(0x0D) || (ch == TCHAR(0x0A)))) {
        Buffer += ch;
        if (inBuffer_ && (inBuffer_.peekChar() == TCHAR(0x0A))) {
          ch = inBuffer_.getChar(); // consume second character (of 2 character sequence)
          Buffer += ch;
        }
        bWhitespaceGenerated = true;
      }
    } else if (bSingleLineComment) {
      if ((ch == TCHAR(0x0D) || (ch == TCHAR(0x0A)))) {
        Buffer += ch;
        if (inBuffer_ && (inBuffer_.peekChar() == TCHAR(0x0A))) {
          ch = inBuffer_.getChar(); // consume second character (of 2 character sequence)
          Buffer += ch;
        }
        bSingleLineComment = false;
      }
    } else {				
      if (bQuoted) {
        Buffer += ch;
        if (!bEscaped) {
          if (ch == TCHAR('\\')) {
            bEscaped = true;
          } else if (ch == matchQuote) 
            bQuoted = false;
        } else {
          bEscaped = false;
        }
      } else if ((ch == TCHAR('/')) && inBuffer_) {
        if (inBuffer_.peekChar() == TCHAR('/')) {
          inBuffer_.getChar(); // consume second character (of 2 character sequence)
          bSingleLineComment = true;
        } else if (inBuffer_.peekChar() == TCHAR('*')) {
          inBuffer_.getChar(); // consume second character (of 2 character sequence)
          bWhitespaceGenerated = false;
          bMultiLineComment = true;
        } else {
          Buffer += ch;
        }
      } else if ((ch == TCHAR('"')) || (ch == TCHAR('\''))) {
        Buffer += ch;
        bQuoted = true;
        matchQuote = ch;
      } else {
        Buffer += ch;
      }
    }
    
  } // while (inBuffer_) (or while (has more input)
} // Process

/* ======================================================================================================
   MacroProcessingFilter
   ======================================================================================================
*/

MacroProcessingFilter::MacroProcessingFilter(const TCHAR * pName, const FString & fileName, FFeedbackContext* Warn) 
  :  packageName_(pName), FTextFilter(fileName, Warn), nestedIfCounter_(0)
{
	emitCharacters_.Push(true);
	initSymbolTable();
} // MacroProcessingFilter
/* Initializes the macro symbol table with built-in macros that can be treated as standard macros (not all can)
*/
void MacroProcessingFilter::initSymbolTable()
{
	if (ParseParam(appCmdLine(), TEXT("debug"))) {
		symbolTable_.Add(TEXT("debug"), TEXT("..."));
	}

#ifdef FINAL_RELEASE
	symbolTable_.Add(TEXT("FINAL_RELEASE"), TEXT("..."));
#endif
} // initSymbolTable

/* inBuffer_ should be pointing at the first character of the name OR a macroBeginNameWrap character;
   getToken will advance to the end of the piece, returning the name (with the wrapper, if present, stripped).
*/
FString MacroProcessingFilter::getName() 
{
	UBOOL wrapped = false;
	TCHAR ch;
	FString retval;
	if (inBuffer_.peekChar() == macroBeginNameWrap) {
		inBuffer_.getChar(); // advance past the wrapper
		wrapped = true;
	}
	while (inBuffer_) {
		ch = inBuffer_.getChar();
		if (!isalnum(ch) && (ch != '#')) {
			inBuffer_.ungetChar(ch);
			break;
		}
		if (ch != 0x00)
			retval += ch;
	}
	
	if (wrapped) {
		ch = inBuffer_.getChar();
		if (ch != macroEndNameWrap) {
			Warn_->Logf(NAME_Warning, TEXT("Unterminated macro name "));
		}
	}
	return retval;
} // getName

/* Called with current char just past openning delimiter of the no-expand region. 
*/
FString MacroProcessingFilter::getUnexpanded()
{
	FString region;
	INT nestingCount = 1; // first set of quotes
	TCHAR lastChar = macroBeginNoExpand;
	while (nestingCount != 0) {
		TCHAR ch = inBuffer_.getChar();
		if (lastChar == macroInvitationChar) {
			if (ch == macroBeginNoExpand) {
				++nestingCount;
			} else if (ch == macroEndNoExpand) {
				--nestingCount;
				if (nestingCount == 0) break;
			}
		}
		if (ch != 0x00) {
			region += ch;
			lastChar = ch;
		}
	}
	/* ----- We have included the ` on the last `) pair; need to get rid of it on return
	   Note that this means the length of region must be greater than 0.
	*/
	region[region.Len() - 1] = '\0';

	return region;
} // getUnexpanded

/* Read on parameter for a macro. It is assumed that we are just past the opening left parenthesis
   or comma. This function will skip leading whitespace and assemble the parameter until it sees a
   top-level (to it) comma or closing parentesis. It will expand macros as they are encountered.
*/
FString MacroProcessingFilter::getParam()
{
	FString param;
	TCHAR ch = inBuffer_.getChar();

	// parameters skip leading blanks
	while (isspace(ch)) ch = inBuffer_.getChar();

	INT nestingLevel = 1; // for the ( at the beginning of the parameters
	
	for (; (nestingLevel != 0); ch = inBuffer_.getChar()) {
		if (ch == '(') 
			++nestingLevel;
		else if ((ch == ')') || ((nestingLevel == 1) && (ch == ','))) {
			--nestingLevel;
			if (nestingLevel == 0) break;
		}

		if (ch != macroInvitationChar) {
			if (emitCharacters_.Top() &&(ch != 0x00))
				param += ch;
			continue;
		}
		/* last character read invites a macro: either a "quoted" or not-expanded region 
		   (where parentheses and commas don't count) or a named macro. */
		if (inBuffer_.peekChar() == macroBeginNoExpand) {
			inBuffer_.getChar(); // skip past macroBeginNoExpand
			param += getUnexpanded();
		} else {
			FString macroName = getName();
			if (emitCharacters_.Top() || isHandledWhenNotEmitted(macroName))
				expand(macroName);
		}
	} // for (;nestingLevel != 0...
	inBuffer_.ungetChar(ch);
	return param;
} // getParam
/* Assumed to be just after the name of a macro in running text; will return the count of
   how many parameters were found and fill Params to that number with the values. No expansion 
   takes place here.
*/
INT MacroProcessingFilter::getParamList(TArray<FString> & Params)
{
	if (inBuffer_.peekChar() != '(') 
		return 0;

	Params.Empty();
	while (inBuffer_.peekChar() != ')') {
		inBuffer_.getChar(); // go past the ( or ,
		FString nextParameter = getParam();
		new (Params) FString(nextParameter);
	}

	inBuffer_.getChar(); // go past the )
	return Params.Num();

} // getParamList
/* Given the name of a macro as a parameter, this routine will expand it. It is in its own function
   so that it can be called on macro parameters as they are being processed. 
*/
FString MacroProcessingFilter::expand(const FString& macroName)
{
	const FString& location = inBuffer_.CurrentLocation();
	FString result;
	FString definition;
	TArray<FString> parameters;

	if (appStrcmp(*macroName, TEXT("if")) == 0) {
		if ((inBuffer_.peekChar() != '(') || (getParamList(parameters)  != 1)) {
			Warn_->Logf(NAME_Warning, TEXT("%s: wrong number of parameters for %s. Expected 1, found %d"), *location, *macroName, parameters.Num());
		} else {
			++nestedIfCounter_;
			if (nestedIfCounter_ == 1) 
				emitCharacters_.Push(parameters(0) != TEXT(""));
			

		}
	} else if (appStrcmp(*macroName, TEXT("else")) == 0) {
		if ((inBuffer_.peekChar() == '(') && (getParamList(parameters)  != 0)) {
			Warn_->Logf(NAME_Warning, TEXT("%s: wrong number of parameters for %s. Expected 0, found %d --- ignored"), *location, *macroName, parameters.Num());
		}
		if ((nestedIfCounter_ == 1) && (emitCharacters_.Num() > 1)) {
			UBOOL switchValue = !emitCharacters_.Pop();
			emitCharacters_.Push(switchValue);
		}
	} else if (appStrcmp(*macroName, TEXT("endif")) == 0) {
		if ((inBuffer_.peekChar() == '(') && (getParamList(parameters)  != 0)) {
			Warn_->Logf(NAME_Warning, TEXT("%s: wrong number of parameters for %s. Expected 0, found %d --- ignored"), *location, *macroName, parameters.Num());
		}
		--nestedIfCounter_;
		if ((nestedIfCounter_ == 0) && (emitCharacters_.Num() > 1)) {
			emitCharacters_.Pop();
		}
	} else if (appStrcmp(*macroName, TEXT("include")) == 0) {
		if ((inBuffer_.peekChar() != '(') || (getParamList(parameters) != 1)) {
			Warn_->Logf(NAME_Warning, TEXT("%s: wrong number of parameters for %s. Expected 1, found %d"), *location, *macroName, parameters.Num());
		} else {
			const FString& fileName = parameters(0);
			FString fileContent=TEXT("");


			if (**fileName != '.' && **fileName != '\\' && **fileName != '/')
			{
				// No special characters in the beginning of the filename - 
				// attempt to search this package's directory
				appLoadFileToString(fileContent, *FString::Printf(TEXT("../%s/Classes/%s"), *packageName_, *fileName));
			}

			if ( fileContent.Len() == 0 )
				appLoadFileToString(fileContent, *fileName);
				
			if (fileContent.Len() != 0) {
				CommentStrippingFilter stripper(fileName, Warn_);
				FString strippedFileContent;
				stripper.Process(*fileContent, *fileContent + fileContent.Len(), strippedFileContent);
				inBuffer_.AddFile(*strippedFileContent, *strippedFileContent + strippedFileContent.Len(), fileName);
			} else {
				Warn_->Logf(NAME_Warning, TEXT("%s: unable to read include file \"%s\""), *location, *fileName);
			}
		}
	} else if (appStrcmp(*macroName, TEXT("define")) == 0) {
		if ((inBuffer_.peekChar() != '(') || (getParamList(parameters)  < 1)) {
			Warn_->Logf(NAME_Warning, TEXT("%s: wrong number of parameters for %s. Expected 1-2, found %d"), *location, *macroName, parameters.Num());
		} else {
			if (parameters.Num() > 2) {
				Warn_->Logf(NAME_Warning, TEXT("%s: extra parameters for %s; ignoring excess."), *location, *macroName);
			}
			const FString& newMacroName = parameters(0);
			if (appIsDigit(newMacroName[0]))
			{
				Warn_->Logf(NAME_Error, TEXT("%s: macro names cannot start with a digit"), *location);
			}
			else
			{
				FString newMacroDefinition = (parameters.Num() > 1) ? parameters(1) : TEXT("");
				symbolTable_.Add(*newMacroName, *newMacroDefinition);
			}
		}
	} else if (appStrcmp(*macroName, TEXT("isdefined")) == 0) {
		if ((inBuffer_.peekChar() != '(') || (getParamList(parameters)  != 1)) {
			Warn_->Logf(NAME_Warning, TEXT("%s: wrong number of parameters for %s. Expected 1, found %d"), *location, *macroName, parameters.Num());
		} else {
			const FString& defMacroName = parameters(0);
			FString dummyDefinition;
			if (lookup(defMacroName, dummyDefinition))
				inBuffer_.ungetChar('1');
		}
	} else if (appStrcmp(*macroName, TEXT("notdefined")) == 0) {
		if ((inBuffer_.peekChar() != '(') || (getParamList(parameters)  != 1)) {
			Warn_->Logf(NAME_Warning, TEXT("%s: wrong number of parameters for %s. Expected 1, found %d"), *location, *macroName, parameters.Num());
		} else {
			const FString& defMacroName = parameters(0);
			FString dummyDefinition;
			if (!lookup(defMacroName, dummyDefinition))
				inBuffer_.ungetChar('1');
		}
	} else if (appStrcmp(*macroName, TEXT("undefine")) == 0) {
		if ((inBuffer_.peekChar() != '(') || (getParamList(parameters)  != 1)) {
			Warn_->Logf(NAME_Warning, TEXT("%s: wrong number of parameters for %s. Expected 1, found %d"), *location, *macroName, parameters.Num());
		} else {
			const FString& undefMacroName = parameters(0);
			FString dummyDefinition;
			if (lookup(undefMacroName, dummyDefinition))
				symbolTable_.Remove(*undefMacroName);
		}
	} else if (lookup(macroName, definition)) {
		if (inBuffer_.peekChar() == '(') {
			getParamList(parameters);
			inBuffer_.AddMacro(*definition, *definition + definition.Len(), macroName, &parameters);
		} else {
			inBuffer_.AddMacro(*definition, *definition + definition.Len(), macroName, NULL);
		}
	}
	// assume all macros starting with a number are valid, since they are used to represent macro parameters
	else if (!appIsDigit(macroName[0]))
	{
		Warn_->Logf(NAME_Error, TEXT("%s: Unknown macro %s"), *location, *macroName);
	}
	return result;
} // expand

UBOOL MacroProcessingFilter::isHandledWhenNotEmitted(const FString & macroName)
{
	return (appStrcmp(*macroName, TEXT("if")) == 0) 
		|| (appStrcmp(*macroName, TEXT("else")) == 0)
		|| (appStrcmp(*macroName, TEXT("endif")) == 0);
} // isHandledWhenNotEmitted

void MacroProcessingFilter::Process(const TCHAR * Begin,
							        const TCHAR * End,
									FString &Buffer)
{
	TCHAR ch;
	inBuffer_.AddFile(Begin, End, fileName_);
	FString Unemitted;
	while (inBuffer_) {
		ch = inBuffer_.getChar();
		if (ch != macroInvitationChar) {
			if ((ch != 0x00) && ((emitCharacters_.Top() || (ch == 0x0D) || (ch == 0x0A)))) {
				if (Unemitted.Len() != 0) {
					debugf(NAME_DevBrian, TEXT("Unemitted characters = \"%s\""), *Unemitted);
					Unemitted = TEXT("");
				}

				Buffer += ch;
			} else {
				Unemitted += ch;
			}
			continue;
		}
		
		// ----- We are looking at the beginning of a macro need to get the name of the macro.
		if (inBuffer_.peekChar() == macroBeginNoExpand) {
			// deal with ignoring a region ----- should be a function for this (since macro parameters use it)
			inBuffer_.getChar(); // skip past macroBeginNoExpand
			if (emitCharacters_.Top())
				Buffer += getUnexpanded();
			else
				getUnexpanded(); // just consume it
		} else {
			FString macroName = getName();
			if (emitCharacters_.Top() || isHandledWhenNotEmitted(macroName))
				expand(macroName);
		}
	} // while (inBuffer_)
} // Process