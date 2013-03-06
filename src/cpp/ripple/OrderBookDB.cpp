#include <boost/foreach.hpp>

#include "Application.h"
#include "OrderBookDB.h"
#include "Log.h"

SETUP_LOG();

OrderBookDB::OrderBookDB() : mSeq(0)
{

}

void OrderBookDB::invalidate()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	mSeq = 0;
}

// TODO: this would be way faster if we could just look under the order dirs
void OrderBookDB::setup(Ledger::ref ledger)
{
	boost::unordered_set<uint256> mSeen;

	boost::recursive_mutex::scoped_lock sl(mLock);

	if (ledger->getLedgerSeq() == mSeq)
		return;
	mSeq = ledger->getLedgerSeq();

	LoadEvent::autoptr ev = theApp->getJobQueue().getLoadEventAP(jtOB_SETUP, "OrderBookDB::setup");

	mXRPOrders.clear();
	mIssuerMap.clear();

	// walk through the entire ledger looking for orderbook entries
	uint256 currentIndex = ledger->getFirstLedgerIndex();

	cLog(lsDEBUG) << "OrderBookDB>";

	while (currentIndex.isNonZero())
	{
		SLE::pointer entry = ledger->getSLEi(currentIndex);
		if ((entry->getType() == ltDIR_NODE) && (entry->isFieldPresent(sfExchangeRate)) &&
			(entry->getFieldH256(sfRootIndex) == currentIndex))
		{
			const uint160& ci = entry->getFieldH160(sfTakerPaysCurrency);
			const uint160& co = entry->getFieldH160(sfTakerGetsCurrency);
			const uint160& ii = entry->getFieldH160(sfTakerPaysIssuer);
			const uint160& io = entry->getFieldH160(sfTakerGetsIssuer);

			uint256 index = Ledger::getBookBase(ci, ii, co, io);
			if (mSeen.insert(index).second)
			{
				OrderBook::pointer book = boost::make_shared<OrderBook>(boost::cref(index),
					boost::cref(ci), boost::cref(co), boost::cref(ii), boost::cref(io));

				if (!book->getCurrencyIn())			// XRP
					mXRPOrders.push_back(book);
				else
					mIssuerMap[book->getIssuerIn()].push_back(book);
			}
		}

		currentIndex = ledger->getNextLedgerIndex(currentIndex);
	}

	cLog(lsDEBUG) << "OrderBookDB<";
}

// return list of all orderbooks that want IssuerID
std::vector<OrderBook::pointer>& OrderBookDB::getBooks(const uint160& issuerID)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	boost::unordered_map< uint160, std::vector<OrderBook::pointer> >::iterator it = mIssuerMap.find(issuerID);
	return (it == mIssuerMap.end())
		? mEmptyVector
		: it->second;
}

// return list of all orderbooks that want this issuerID and currencyID
void OrderBookDB::getBooks(const uint160& issuerID, const uint160& currencyID, std::vector<OrderBook::pointer>& bookRet)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	boost::unordered_map< uint160, std::vector<OrderBook::pointer> >::iterator it = mIssuerMap.find(issuerID);
	if (it != mIssuerMap.end())
	{
		BOOST_FOREACH(OrderBook::ref book, it->second)
		{
			if (book->getCurrencyIn() == currencyID)
				bookRet.push_back(book);
		}
	}
}

BookListeners::pointer OrderBookDB::makeBookListeners(const uint160& currencyIn, const uint160& currencyOut,
	const uint160& issuerIn, const uint160& issuerOut)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	BookListeners::pointer ret = getBookListeners(currencyIn, currencyOut, issuerIn, issuerOut);
	if (!ret)
	{
		ret = boost::make_shared<BookListeners>();
		mListeners[issuerIn][issuerOut][currencyIn][currencyOut] = ret;
	}
	return ret;
}

BookListeners::pointer OrderBookDB::getBookListeners(const uint160& currencyIn, const uint160& currencyOut,
	const uint160& issuerIn, const uint160& issuerOut)
{
	BookListeners::pointer ret;
	boost::recursive_mutex::scoped_lock sl(mLock);

	std::map<uint160, std::map<uint160, std::map<uint160, std::map<uint160, BookListeners::pointer> > > >::iterator
		it0 = mListeners.find(issuerIn);
	if(it0 == mListeners.end())
		return ret;

	std::map<uint160, std::map<uint160, std::map<uint160, BookListeners::pointer> > >::iterator
		it1 = (*it0).second.find(issuerOut);
	if(it1 == (*it0).second.end())
		return ret;

	std::map<uint160, std::map<uint160, BookListeners::pointer> >::iterator it2 = (*it1).second.find(currencyIn);
	if(it2 == (*it1).second.end())
		return ret;

	std::map<uint160, BookListeners::pointer>::iterator it3 = (*it2).second.find(currencyOut);
	if(it3 == (*it2).second.end())
		return ret;

	return (*it3).second;
}

/*
"CreatedNode" : {
"LedgerEntryType" : "Offer",
"LedgerIndex" : "F353BF8A7DCE35EA2985596F4C8421E30EF3B9A21618566BFE0ED00B62A8A5AB",
"NewFields" : {
"Account" : "rB5TihdPbKgMrkFqrqUC3yLdE8hhv4BdeY",
"BookDirectory" : "FF26BE244767D0EA9EFD523941439009E4185E4CBB918F714C08E1BC9BF04000",
"Sequence" : 112,
"TakerGets" : "400000000",
"TakerPays" : {
"currency" : "BTC",
"issuer" : "r3kmLJN5D28dHuH8vZNUZpMC43pEHpaocV",
"value" : "1"
}
}
}

"ModifiedNode" : {
"FinalFields" : {
"Account" : "rHTxKLzRbniScyQFGMb3NodmxA848W8dKM",
"BookDirectory" : "407AF8FFDE71114B1981574FDDA9B0334572D56FC579735B4B0BD7A625405555",
"BookNode" : "0000000000000000",
"Flags" : 0,
"OwnerNode" : "0000000000000000",
"Sequence" : 32,
"TakerGets" : "149900000000",
"TakerPays" : {
"currency" : "USD",
"issuer" : "r9vbV3EHvXWjSkeQ6CAcYVPGeq7TuiXY2X",
"value" : "49.96666666666667"
}
},
"LedgerEntryType" : "Offer",
"LedgerIndex" : "C60F8CC514208FA5F7BD03CF1B64B38B7183CD52318FCBBD3726350D4FE693B0",
"PreviousFields" : {
"TakerGets" : "150000000000",
"TakerPays" : {
"currency" : "USD",
"issuer" : "r9vbV3EHvXWjSkeQ6CAcYVPGeq7TuiXY2X",
"value" : "50"
}
},
"PreviousTxnID" : "1A6AAE3F1AC5A8A7554A5ABC395D17FED5BF62CD90181AA8E4315EDFED4EDEB3",
"PreviousTxnLgrSeq" : 140734
}

*/
// Based on the meta, send the meta to the streams that are listening 
// We need to determine which streams a given meta effects
void OrderBookDB::processTxn(Ledger::ref ledger, const ALTransaction& alTx, Json::Value& jvObj)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if (alTx.getResult() == tesSUCCESS)
	{
		// check if this is an offer or an offer cancel or a payment that consumes an offer
		//check to see what the meta looks like
		BOOST_FOREACH(STObject& node, alTx.getMeta()->getNodes())
		{
			try
			{
				if (node.getFieldU16(sfLedgerEntryType) == ltOFFER)
				{
					SField* field=NULL;

					if (node.getFName() == sfModifiedNode)
					{
						field = &sfPreviousFields;
					}
					else if (node.getFName() == sfCreatedNode)
					{
						field = &sfNewFields;
					}
					else if (node.getFName() == sfDeletedNode)
					{
						field = &sfFinalFields;
					}

					if (field)
					{
						const STObject* data = dynamic_cast<const STObject*>(node.peekAtPField(*field));
						if (data)
						{
							STAmount takerGets = data->getFieldAmount(sfTakerGets);
							uint160 currencyOut = takerGets.getCurrency();
							uint160 issuerOut = takerGets.getIssuer();

							STAmount takerPays = data->getFieldAmount(sfTakerPays);
							uint160 currencyIn = takerPays.getCurrency();
							uint160 issuerIn = takerPays.getIssuer();

							// determine the OrderBook
							BookListeners::pointer book =
								getBookListeners(currencyIn, currencyOut, issuerIn, issuerOut);
							if (book)
								book->publish(jvObj);
						}
					}
				}
			}
			catch (...)
			{
				cLog(lsINFO) << "Fields not found in OrderBookDB::processTxn";
			}
		}
	}
}

void BookListeners::addSubscriber(InfoSub::ref sub)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	mListeners[sub->getSeq()] = sub;
}

void BookListeners::removeSubscriber(uint64 seq)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	mListeners.erase(seq);
}

void BookListeners::publish(Json::Value& jvObj)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	NetworkOPs::subMapType::const_iterator it = mListeners.begin();
	while (it != mListeners.end())
	{
		InfoSub::pointer p = it->second.lock();
		if (p)
		{
			p->send(jvObj, true);
			++it;
		}
		else
			it = mListeners.erase(it);
	}
}

// vim:ts=4
