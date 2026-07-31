#ifndef GAME_BLAST_PATTERN_H
#define GAME_BLAST_PATTERN_H

///////////////////////////////////////////////////////////////////////////////
class VBlastPattern : public IDetour
{
	virtual void GetAdr(void) const;
	virtual void GetFun(void) const;
	virtual void GetVar(void) const;
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // GAME_BLAST_PATTERN_H
