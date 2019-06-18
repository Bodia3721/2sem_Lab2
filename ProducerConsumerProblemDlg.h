
// ProducerConsumerProblemDlg.h : header file
//

#pragma once


// CProducerConsumerProblemDlg dialog
class CProducerConsumerProblemDlg : public CDialogEx
{
private:
	CProducerConsumerProblemDlg(CWnd* pParent = NULL);
public:
	// pattern: singleton
	static CProducerConsumerProblemDlg& Get();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_PRODUCERCONSUMERPROBLEM_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support


// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedButtonstart();
	int producerSleepTimeView;
	CString outputView;
};
