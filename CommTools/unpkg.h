#pragma once
#include "pkg8583Parser.h"

class CUnpkg :
	public T8583Parser
{
public:
	virtual ~CUnpkg();
	virtual void LoadFieldFormatDef();
	void SetFormatDef(TFieldFormat FieldFormat[]);
};
