/**
 * File manager that knows the full TOC of the DVD
 */
class FFileManagerTOC : public FFileManagerXbox
{
	/**
	 * The array of table of contents entries
	 */
	FFileData* TOCData;

	/**
	 * The number of entries for searching
	 */
	DWORD NumEntries;

public:
	FFileManagerTOC(void)
	{
