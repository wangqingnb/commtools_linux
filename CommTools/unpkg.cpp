//Written by Rocky Wang 2016.7
#include "CommTools.h"
#include "pkg8583.h"
#include "unpkg.h"
#include <string.h>
void CUnpkg::LoadFieldFormatDef()
{
}

void CUnpkg::SetFormatDef(TFieldFormat FieldFormat[])
{
	memcpy(this->FieldFormat, FieldFormat, sizeof(TFieldFormat) * FIELD_COUNT_MAX);
}

CUnpkg::~CUnpkg()
{

}